#include "main/host/thread_ptrace.h"

#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <x86intrin.h>

// Must come after sys/ptrace.h
#include <linux/ptrace.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/thread_protected.h"
#include "main/host/tsc.h"
#include "support/logger/logger.h"

#define THREADPTRACE_TYPE_ID 3024

// glibc in centos 7 does not include the following, but it does use a supported
// kernel (3.10 > 3.8)
#if !defined(PTRACE_O_EXITKILL)
#define PTRACE_O_EXITKILL (1 << 20)
#endif

#define THREADPTRACE_PTRACE_OPTIONS (PTRACE_O_EXITKILL | PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC)

static char SYSCALL_INSTRUCTION[] = {0x0f, 0x05};

typedef enum {
    // Doesn't exist yet.
    THREAD_PTRACE_CHILD_STATE_NONE = 0,
    // Waiting for initial ptrace call.
    THREAD_PTRACE_CHILD_STATE_TRACE_ME,
    THREAD_PTRACE_CHILD_STATE_SYSCALL,
    THREAD_PTRACE_CHILD_STATE_EXECVE,
    THREAD_PTRACE_CHILD_STATE_SIGNALLED,
    THREAD_PTRACE_CHILD_STATE_EXITED,
} ThreadPtraceChildState;

typedef struct _PendingWrite {
    PluginPtr pluginPtr;
    void *ptr;
    size_t n;
} PendingWrite;

typedef enum {
    STOPREASON_EXITED_NORMAL,
    STOPREASON_EXITED_SIGNAL,
    STOPREASON_SIGNAL,
    STOPREASON_SYSCALL,
    STOPREASON_EXEC,
    STOPREASON_CONTINUED,
    STOPREASON_UNKNOWN,
} StopReasonType;

typedef struct {
    StopReasonType type;
    union {
        struct {
            int exit_code;
        } exited_normal;
        struct {
            int signal;
        } exited_signal;
        struct {
            int signal;
        } signal;
    };
} StopReason;

static StopReason _getStopReason(int wstatus) {
    StopReason rv;
    if (WIFSIGNALED(wstatus)) {
        rv = (StopReason){
            .type = STOPREASON_EXITED_SIGNAL,
            .exited_signal.signal = WTERMSIG(wstatus),
        };
        debug("STOPREASON_EXITED_SIGNAL: %d", rv.exited_signal.signal);
        return rv;
    } else if (WIFEXITED(wstatus)) {
        rv = (StopReason){
            .type = STOPREASON_EXITED_NORMAL,
            .exited_normal.exit_code = WEXITSTATUS(wstatus),
        };
        debug("STOPREASON_EXITED_NORMAL: %d", rv.exited_normal.exit_code);
        return rv;
    } else if (WIFSTOPPED(wstatus)) {
        const int signal = WSTOPSIG(wstatus);
        if (signal == (SIGTRAP | 0x80)) {
            // See PTRACE_O_TRACESYSGOOD in ptrace(2).
            rv = (StopReason){
                .type = STOPREASON_SYSCALL,
            };
            debug("STOPREASON_SYSCALL");
            return rv;
        } else if (wstatus >> 8 == (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
            // See PTRACE_O_TRACEEXEC in ptrace(2).
            rv = (StopReason){
                .type = STOPREASON_EXEC,
            };
            debug("STOPREASON_EXEC");
            return rv;
        } else {
            rv = (StopReason){
                .type = STOPREASON_SIGNAL,
                .signal.signal = WSTOPSIG(wstatus),
            };
            debug("STOPREASON_SIGNAL: %d", rv.signal.signal);
            return rv;
        }
    } else if (WIFCONTINUED(wstatus)) {
        rv = (StopReason){
            .type = STOPREASON_CONTINUED,
        };
        debug("STOPREASON_CONTINUED");
        return rv;
    } else {
        rv = (StopReason){
            .type = STOPREASON_UNKNOWN,
        };
        debug("STOPREASON_UNKNOWN");
        return rv;
    }
}

typedef struct _ThreadPtrace {
    Thread base;

    SysCallHandler* sys;

    // GArray of PendingWrite, to be flushed before returning control to the
    // plugin.
    GArray* pendingWrites;

    // GArray of void*s that were previously returned by
    // threadptrace_readPluginPtr. These should be freed before returning
    // control to the plugin.
    GArray* readPointers;

    Tsc tsc;

    FILE* childMemFile;

    int threadID;

    // Reason for the most recent transfer of control back to Shadow.
    ThreadPtraceChildState childState;

    int returnCode;

    // use for SYSCALL
    struct {
        struct user_regs_struct regs;
        SysCallReturn sysCallReturn;
    } syscall;

    // Whenever we use ptrace to continue we may raise a signal.  Currently we
    // only use this to allow a signal that was already raise (e.g. SIGSEGV) to
    // be delivered.
    intptr_t signalToDeliver;

    // True if we have detached ptrace from the plugin and should attach before
    // executing another ptrace operation.
    bool needAttachment;
} ThreadPtrace;

// Forward declaration.
static void _threadptrace_memcpyToPlugin(ThreadPtrace* thread,
                                         PluginPtr plugin_dst, void* shadow_src,
                                         size_t n);
const void* threadptrace_getReadablePtr(Thread* base, PluginPtr plugin_src,
                                        size_t n);

static ThreadPtrace* _threadToThreadPtrace(Thread* thread) {
    utility_assert(thread->type_id == THREADPTRACE_TYPE_ID);
    return (ThreadPtrace*)thread;
}

static Thread* _threadPtraceToThread(ThreadPtrace* thread) {
    return (Thread*)thread;
}

static pid_t _threadptrace_fork_exec(const char* file, char* const argv[],
                                     char* const envp[]) {
    pid_t shadow_pid = getpid();
    pid_t pid = fork();

    switch (pid) {
        case -1: {
            error("fork: %s", g_strerror(errno));
            return -1;
        }
        case 0: {
            // child
 
            // Ensure that the child process exits when Shadow does.  Shadow
            // ought to have already tried to terminate the child via SIGTERM
            // before shutting down (though see
            // https://github.com/shadow/shadow/issues/903), so now we jump all
            // the way to SIGKILL.
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
                error("prctl: %s", g_strerror(errno));
                exit(1);
            }
            // Validate that Shadow is still alive (didn't die in between forking and calling
            // prctl).
            if (getppid() != shadow_pid) {
                error("parent (shadow) exited");
                exit(1);
            }
            // Disable RDTSC
            if (prctl(PR_SET_TSC, PR_TSC_SIGSEGV, 0, 0, 0) < 0) {
                error("prctl: %s", g_strerror(errno));
                return -1;
            }
            // Allow parent to trace.
            if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
                error("ptrace: %s", g_strerror(errno));
                return -1;
            }
            // Wait for parent to attach.
            if (raise(SIGSTOP) < 0) {
                error("raise: %s", g_strerror(errno));
                return -1;
            }
            if (execvpe(file, argv, envp) < 0) {
                error("execvpe: %s", g_strerror(errno));
                return -1;
            }
        }
        default: {
            // parent
            info("started process %s with PID %d", file, pid);
            return pid;
        }
    }
}

static void _threadptrace_getChildMemoryHandle(ThreadPtrace* thread) {
    char path[64];
    snprintf(path, 64, "/proc/%d/mem", thread->base.nativePid);

    bool reopen = false;
    if (thread->childMemFile) {
        thread->childMemFile = freopen(path, "r+", thread->childMemFile);
        reopen = true;
    } else {
        thread->childMemFile = fopen(path, "r+");
    }

    if (thread->childMemFile == NULL) {
        error("%s %s: %s", reopen ? "freopen" : "fopen", path, g_strerror(errno));
        return;
    }
}

static void _threadptrace_enterStateTraceMe(ThreadPtrace* thread) {
    // PTRACE_O_EXITKILL: Kill child if our process dies.
    // PTRACE_O_TRACESYSGOOD: Handle syscall stops explicitly.
    // PTRACE_O_TRACEEXEC: Handle execve stops explicitly.
    if (ptrace(PTRACE_SETOPTIONS, thread->base.nativePid, 0, THREADPTRACE_PTRACE_OPTIONS) < 0) {
        error("ptrace: %s", strerror(errno));
        return;
    }
    // Get a new handle to the child's memory.
    thread->childMemFile = NULL;
    _threadptrace_getChildMemoryHandle(thread);
}

static void _threadptrace_enterStateExecve(ThreadPtrace* thread) {
    // We have to reopen the handle to child's memory.
    _threadptrace_getChildMemoryHandle(thread);
}

static void _threadptrace_enterStateSyscall(ThreadPtrace* thread) {
    struct user_regs_struct* regs = &thread->syscall.regs;
    if (ptrace(PTRACE_GETREGS, thread->base.nativePid, 0, regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        return;
    }
}

static void _threadptrace_enterStateSignalled(ThreadPtrace* thread,
                                              int signal) {
    thread->childState = THREAD_PTRACE_CHILD_STATE_SIGNALLED;
    if (signal == SIGSEGV) {
        struct user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, thread->base.nativePid, 0, &regs) < 0) {
            error("ptrace: %s", g_strerror(errno));
            return;
        }
        uint64_t eip = regs.rip;
        const uint8_t* buf = thread_getReadablePtr(
            _threadPtraceToThread(thread), (PluginPtr){eip}, 16);
        if (isRdtsc(buf)) {
            debug("emulating rdtsc");
            Tsc_emulateRdtsc(&thread->tsc,
                             &regs,
                             worker_getCurrentTime() / SIMTIME_ONE_NANOSECOND);
            if (ptrace(PTRACE_SETREGS, thread->base.nativePid, 0, &regs) < 0) {
                error("ptrace: %s", g_strerror(errno));
                return;
            }
            return;
        }
        if (isRdtscp(buf)) {
            debug("emulating rdtscp");
            Tsc_emulateRdtscp(&thread->tsc,
                              &regs,
                              worker_getCurrentTime() / SIMTIME_ONE_NANOSECOND);
            if (ptrace(PTRACE_SETREGS, thread->base.nativePid, 0, &regs) < 0) {
                error("ptrace: %s", g_strerror(errno));
                return;
            }
            return;
        }
        // Do not use `error` here, since that'll cause us to immediately abort
        // in debug builds. Better to let the SIGSEGV be delivered so that it
        // can generate a core file for debugging.
        warning(
            "Unhandled SIGSEGV addr:%016lx contents:%x %x %x %x %x %x %x %x",
            eip, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
            buf[7]);
        // fall through
    }
    // Deliver the signal.
    warning("Delivering signal %d", signal);
    thread->signalToDeliver = signal;
}

static void _threadptrace_updateChildState(ThreadPtrace* thread, StopReason reason) {
    switch(reason.type) {
        case STOPREASON_EXITED_SIGNAL:
            debug("child %d terminated by signal %d", thread->base.nativePid,
                  reason.exited_signal.signal);
            thread->childState = THREAD_PTRACE_CHILD_STATE_EXITED;
            thread->returnCode = -1;
            return;
        case STOPREASON_EXITED_NORMAL:
            thread->childState = THREAD_PTRACE_CHILD_STATE_EXITED;
            thread->returnCode = reason.exited_normal.exit_code;
            return;
        case STOPREASON_EXEC:
            thread->childState = THREAD_PTRACE_CHILD_STATE_EXECVE;
            _threadptrace_enterStateExecve(thread);
            return;
        case STOPREASON_SYSCALL:
            thread->childState = THREAD_PTRACE_CHILD_STATE_SYSCALL;
            _threadptrace_enterStateSyscall(thread);
            return;
        case STOPREASON_SIGNAL:
            if (reason.signal.signal == SIGSTOP &&
                thread->childState == THREAD_PTRACE_CHILD_STATE_NONE) {
                // We caught the "raise(SIGSTOP)" just after forking.
                thread->childState = THREAD_PTRACE_CHILD_STATE_TRACE_ME;
                _threadptrace_enterStateTraceMe(thread);
                return;
            }
            _threadptrace_enterStateSignalled(thread, reason.signal.signal);
            return;
        case STOPREASON_CONTINUED:
        default: error("Unhandled stop reason. stop type: %d", reason.type); return;
    }
}

static void _threadptrace_nextChildState(ThreadPtrace* thread) {
    // Wait for child to stop.
    int wstatus;
    if (waitpid(thread->base.nativePid, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        return;
    }
    StopReason reason = _getStopReason(wstatus);
    _threadptrace_updateChildState(thread, reason);
}

pid_t threadptrace_run(Thread* base, gchar** argv, gchar** envv) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    thread->base.nativePid = _threadptrace_fork_exec(argv[0], argv, envv);

    _threadptrace_nextChildState(thread);

    return thread->base.nativePid;
}

static void _threadptrace_handleSyscall(ThreadPtrace* thread) {
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL);
    struct user_regs_struct* regs = &thread->syscall.regs;

    SysCallArgs args = {
        .number = regs->orig_rax,
        .args = {regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8,
                 regs->r9},
    };

    thread->syscall.sysCallReturn =
        syscallhandler_make_syscall(thread->sys, &args);
}

static void _threadptrace_flushPtrs(ThreadPtrace* thread) {
    // Free any read pointers
    if (thread->readPointers->len > 0) {
        for (int i = 0; i < thread->readPointers->len; ++i) {
            void* p = g_array_index(thread->readPointers, void*, i);
            g_free(p);
        }
        thread->readPointers = g_array_set_size(thread->readPointers, 0);
    }
    // Perform writes if needed
    if (thread->pendingWrites->len > 0) {
        for (int i = 0; i < thread->pendingWrites->len; ++i) {
            PendingWrite* write =
                &g_array_index(thread->pendingWrites, PendingWrite, i);
            _threadptrace_memcpyToPlugin(
                thread, write->pluginPtr, write->ptr, write->n);
            g_free(write->ptr);
        }
        thread->pendingWrites = g_array_set_size(thread->pendingWrites, 0);
    }
    // Flush the mem file. In addition to flushing any pending writes, this
    // invalidates the *input* buffers, which are no longer valid after
    // returning control to the child.
    if (fflush(thread->childMemFile) != 0) {
        error("fflush: %s", g_strerror(errno));
    }
}

static void threadptrace_flushPtrs(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    _threadptrace_flushPtrs(thread);
}

static void _threadptrace_doAttach(ThreadPtrace* thread) {
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL);

    debug("thread %i attaching to child %i", thread->base.threadID, (int)thread->base.nativePid);
    if (ptrace(PTRACE_ATTACH, thread->base.nativePid, 0, 0) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }
    int wstatus;
    if (waitpid(thread->base.nativePid, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        abort();
    }
    StopReason reason = _getStopReason(wstatus);
    utility_assert(reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGSTOP);

    if (ptrace(PTRACE_SETOPTIONS, thread->base.nativePid, 0, THREADPTRACE_PTRACE_OPTIONS) < 0) {
        error("ptrace: %s", strerror(errno));
        return;
    }

#if DEBUG
    // Check that regs are where we left them.
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, thread->base.nativePid, 0, &regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }
    utility_assert(!memcmp(&regs, &thread->syscall.regs, sizeof(regs)));
#endif

    // Should cause syscall we stopped at to be re-executed, putting us back in syscall state
    while (reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGSTOP) {
        if (ptrace(PTRACE_SYSEMU, thread->base.nativePid, 0, 0) < 0) {
            error("ptrace: %s", g_strerror(errno));
            abort();
        }
        if (waitpid(thread->base.nativePid, &wstatus, 0) < 0) {
            error("waitpid: %s", g_strerror(errno));
            abort();
        }
        reason = _getStopReason(wstatus);
    }
    if (reason.type != STOPREASON_SYSCALL) {
        error("unexpected stop reason: %d", reason.type);
        utility_assert(reason.type == STOPREASON_SYSCALL);
    }

    // Restore our saved instruction pointer to where it was before we detached - just after the
    // syscall instruction.
    thread->syscall.regs.rip += sizeof(SYSCALL_INSTRUCTION);

#if DEBUG
    // Check that rip is where we expect.
    if (ptrace(PTRACE_GETREGS, thread->base.nativePid, 0, &regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }
    utility_assert(regs.rip == thread->syscall.regs.rip);
#endif

    thread->needAttachment = false;
}

static void _threadptrace_doDetach(ThreadPtrace* thread) {
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL);

    // First rewind the instruction pointer so that the current syscall will be
    // retried when we resume. This isn't *strictly* necessary, but it's not
    // super clear from the ptrace documentation exactly how many SIGSTOP stops
    // to expect when reattaching; experimentally it's 3, but it's unclear
    // whether we can rely on it to always be three. Conversely if we rewind
    // the instruction pointer, when reattaching we can just swallow sigstops
    // until we get a syscall stop.
    thread->syscall.regs.rip -= sizeof(SYSCALL_INSTRUCTION);
#ifdef DEBUG
    // Verify that rip is now pointing at a syscall instruction.
    const uint8_t* buf =
        thread_getReadablePtr(_threadPtraceToThread(thread), (PluginPtr){thread->syscall.regs.rip},
                              sizeof(SYSCALL_INSTRUCTION));
    utility_assert(!memcmp(buf, SYSCALL_INSTRUCTION, sizeof(SYSCALL_INSTRUCTION)));
#endif
    if (ptrace(PTRACE_SETREGS, thread->base.nativePid, 0, &thread->syscall.regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }

    // Send a SIGSTOP. While experimentally just calling PTRACE_DETACH with a
    // SIGSTOP works, ptrace(2) says that it *might* not if we're not already
    // in a signal-delivery-stop.
    if (syscall(SYS_tgkill, thread->base.nativePid, thread->base.nativeTid, SIGSTOP) < 0) {
        error("kill: %s", g_strerror(errno));
        abort();
    }

    // Continue and wait for the signal delivery stop.
    debug("thread %i detaching from child %i", thread->base.threadID, (int)thread->base.nativePid);
    if (ptrace(PTRACE_CONT, thread->base.nativePid, 0, 0) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }
    int wstatus;
    if (waitpid(thread->base.nativePid, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        abort();
    }
    StopReason reason = _getStopReason(wstatus);
    utility_assert(reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGSTOP);
    debug("Stop reason after cont with sigstop: %d", reason.type);

    // Detach, allowing the sigstop to be delivered.
    if (ptrace(PTRACE_DETACH, thread->base.nativePid, 0, SIGSTOP) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }

    thread->needAttachment = true;
}

void threadptrace_detach(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    _threadptrace_doDetach(thread);
}

SysCallCondition* threadptrace_resume(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    if (thread->needAttachment) {
        _threadptrace_doAttach(thread);
    }

    while (true) {
        switch (thread->childState) {
            case THREAD_PTRACE_CHILD_STATE_NONE:
                debug("THREAD_PTRACE_CHILD_STATE_NONE");
                utility_assert(false);
                break;
            case THREAD_PTRACE_CHILD_STATE_TRACE_ME:
                debug("THREAD_PTRACE_CHILD_STATE_TRACE_ME");
                break;
            case THREAD_PTRACE_CHILD_STATE_SYSCALL:
                debug("THREAD_PTRACE_CHILD_STATE_SYSCALL");

                // Ask the syscall handler to handle it.
                _threadptrace_handleSyscall(thread);

                switch (thread->syscall.sysCallReturn.state) {
                    case SYSCALL_BLOCK:
                        return thread->syscall.sysCallReturn.cond;
                    case SYSCALL_DONE:
                        // Return the specified result.
                        thread->syscall.regs.rax = thread->syscall.sysCallReturn.retval.as_u64;
                        if (ptrace(PTRACE_SETREGS, thread->base.nativePid, 0,
                                   &thread->syscall.regs) < 0) {
                            error("ptrace: %s", g_strerror(errno));
                            return NULL;
                        }
                        break;
                    case SYSCALL_NATIVE: {
                        // Have the plugin execute the original syscall
                        struct user_regs_struct* regs = &thread->syscall.regs;
                        thread_nativeSyscall(base, regs->orig_rax, regs->rdi, regs->rsi, regs->rdx,
                                             regs->r10, regs->r8, regs->r9);
                        if (thread->childState != THREAD_PTRACE_CHILD_STATE_SYSCALL) {
                            // Executing the syscall changed our state. We need to process it before
                            // waiting again.
                            continue;
                        }
                        break;
                    }
                }
                break;
            case THREAD_PTRACE_CHILD_STATE_EXECVE:
                debug("THREAD_PTRACE_CHILD_STATE_EXECVE");
                break;
            case THREAD_PTRACE_CHILD_STATE_EXITED:
                debug("THREAD_PTRACE_CHILD_STATE_EXITED");
                return NULL;
            case THREAD_PTRACE_CHILD_STATE_SIGNALLED:
                debug("THREAD_PTRACE_CHILD_STATE_SIGNALLED");
                break;
                // no default
        }
        _threadptrace_flushPtrs(thread);
        // Allow child to start executing.
        if (ptrace(PTRACE_SYSEMU, thread->base.nativePid, 0, thread->signalToDeliver) < 0) {
            error("ptrace %d: %s", thread->base.nativePid, g_strerror(errno));
            return NULL;
        }
        thread->signalToDeliver = 0;
        _threadptrace_nextChildState(thread);
    }
}

bool threadptrace_isRunning(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    switch (thread->childState) {
        case THREAD_PTRACE_CHILD_STATE_TRACE_ME:
        case THREAD_PTRACE_CHILD_STATE_SYSCALL:
        case THREAD_PTRACE_CHILD_STATE_SIGNALLED:
        case THREAD_PTRACE_CHILD_STATE_EXECVE: return true;
        case THREAD_PTRACE_CHILD_STATE_NONE:
        case THREAD_PTRACE_CHILD_STATE_EXITED: return false;
    }
    utility_assert(false);
    return false;
}

void threadptrace_terminate(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    /* make sure we cleanup circular refs */
    if (thread->sys) {
        syscallhandler_unref(thread->sys);
        thread->sys = NULL;
    }

    if (!threadptrace_isRunning(base)) {
        return;
    }

    if (ptrace(PTRACE_CONT, thread->base.nativePid, 0, SIGTERM) < 0) {
        warning("ptrace %d: %s", thread->base.nativePid, g_strerror(errno));
    }
}

int threadptrace_getReturnCode(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_EXITED);
    return thread->returnCode;
}

void threadptrace_free(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    if (thread->sys) {
        syscallhandler_unref(thread->sys);
    }

    worker_countObject(OBJECT_TYPE_THREAD_PTRACE, COUNTER_TYPE_FREE);
}

static void _threadptrace_memcpyToShadow(ThreadPtrace* thread, void* shadow_dst,
                                         PluginPtr plugin_src, size_t n) {
    clearerr(thread->childMemFile);
    if (fseek(thread->childMemFile, plugin_src.val, SEEK_SET) < 0) {
        error("fseek %p: %s", (void*)plugin_src.val, g_strerror(errno));
        return;
    }
    size_t count = fread(shadow_dst, 1, n, thread->childMemFile);
    if (count != n) {
        if (feof(thread->childMemFile)) {
            error("EOF");
            return;
        }
        error("fread %zu -> %zu: %s", n, count, g_strerror(errno));
    }
    return;
}

static void _threadptrace_memcpyToPlugin(ThreadPtrace* thread,
                                         PluginPtr plugin_dst, void* shadow_src,
                                         size_t n) {
    if (fseek(thread->childMemFile, plugin_dst.val, SEEK_SET) < 0) {
        error("fseek %p: %s", (void*)plugin_dst.val, g_strerror(errno));
        return;
    }
    size_t count = fwrite(shadow_src, 1, n, thread->childMemFile);
    if (count != n) {
        error("fwrite %zu -> %zu: %s", n, count, g_strerror(errno));
    }
    return;
}

void* threadptrace_newClonedPtr(Thread* base, PluginPtr plugin_src, size_t n) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    void* rv = g_new(void, n);
    _threadptrace_memcpyToShadow(thread, rv, plugin_src, n);
    return rv;
}

void threadptrace_releaseClonedPtr(Thread* base, void* p) { g_free(p); }

const void* threadptrace_getReadablePtr(Thread* base, PluginPtr plugin_src,
                                        size_t n) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    void* rv = g_new(void, n);
    g_array_append_val(thread->readPointers, rv);
    _threadptrace_memcpyToShadow(thread, rv, plugin_src, n);
    return rv;
}

int threadptrace_getReadableString(Thread* base, PluginPtr plugin_src, size_t n,
                             const char** out_str, size_t* strlen) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    char *str = g_new(char, n);
    int err = 0;

    clearerr(thread->childMemFile);
    if (fseek(thread->childMemFile, plugin_src.val, SEEK_SET) < 0) {
        info("fseek %p: %s", (void*)plugin_src.val, g_strerror(errno));
        err = -EFAULT;
    }
    size_t count = 0;
    while (!err) {
        if (count >= n) {
            err = -ENAMETOOLONG;
            break;
        }
        int c = fgetc(thread->childMemFile);
        if (c == EOF) {
            err = -EFAULT;
            break;
        }
        str[count++] = c;
        if (c == '\0') {
            break;
        }
    }
    if (err != 0) {
        g_free(str);
        return err;
    }
    str = g_realloc(str, count);
    g_array_append_val(thread->readPointers, str);
    utility_assert(out_str);
    *out_str = str;
    if (strlen) {
        *strlen = count-1;
    }
    return 0;
}

void* threadptrace_getWriteablePtr(Thread* base, PluginPtr plugin_src,
                                   size_t n) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    void* rv = g_new(void, n);
    PendingWrite pendingWrite = {.pluginPtr = plugin_src, .ptr = rv, .n = n};
    g_array_append_val(thread->pendingWrites, pendingWrite);
    return rv;
}

void* threadptrace_getMutablePtr(Thread* base, PluginPtr plugin_src, size_t n) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    void* rv = g_new(void, n);
    _threadptrace_memcpyToShadow(thread, rv, plugin_src, n);
    PendingWrite pendingWrite = {.pluginPtr = plugin_src, .ptr = rv, .n = n};
    g_array_append_val(thread->pendingWrites, pendingWrite);
    return rv;
}

long threadptrace_nativeSyscall(Thread* base, long n, va_list args) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    // Unimplemented for other states.
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL);
    // The last ptrace stop was just before executing a syscall instruction.
    // We'll use that to execute the desired syscall, and then restore the
    // original state.

    // Inject the requested syscall number and arguments.
    struct user_regs_struct regs = thread->syscall.regs;
    regs.rax = n;
    regs.rdi = va_arg(args, long);
    regs.rsi = va_arg(args, long);
    regs.rdx = va_arg(args, long);
    regs.r10 = va_arg(args, long);
    regs.r8 = va_arg(args, long);
    regs.r9 = va_arg(args, long);

    // Rewind instruction pointer to point to the syscall instruction again.
    regs.rip -= sizeof(SYSCALL_INSTRUCTION); // Size of the syscall instruction.

    uint64_t syscall_rip = regs.rip;

    debug("threadptrace_nativeSyscall setting regs: rip=%llx n=%llx arg0=%llx arg1=%llx arg2=%llx "
          "arg3=%llx arg4=%llx arg5=%llx",
          regs.rip, regs.rax, regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);

#ifdef DEBUG
    // Verify that rip is now pointing at a syscall instruction.
    const uint8_t* buf = thread_getReadablePtr(
        _threadPtraceToThread(thread), (PluginPtr){regs.rip}, sizeof(SYSCALL_INSTRUCTION));
    utility_assert(!memcmp(buf, SYSCALL_INSTRUCTION, sizeof(SYSCALL_INSTRUCTION)));
#endif

    if (ptrace(PTRACE_SETREGS, thread->base.nativePid, 0, &regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }

    // Single-step until the syscall instruction is executed. It's not clear whether we can depend
    // on stopping the exact same number of times here.
    do {
        if (ptrace(PTRACE_SINGLESTEP, thread->base.nativePid, 0, 0) < 0) {
            error("ptrace %d: %s", thread->base.nativePid, g_strerror(errno));
            abort();
        }
        int wstatus;
        if (waitpid(thread->base.nativePid, &wstatus, 0) < 0) {
            error("waitpid: %s", g_strerror(errno));
            abort();
        }
        StopReason reason = _getStopReason(wstatus);
        if (reason.type != STOPREASON_SIGNAL || reason.signal.signal != SIGTRAP) {
            // In particular this could be an exec stop if the syscall was execve,
            // or an exited stop if the syscall was exit.
            _threadptrace_updateChildState(thread, reason);
        }
        if (!threadptrace_isRunning(base)) {
            // Since the child is no longer running, we have no way of retrieving a
            // return value, if any. e.g. this happens after the `exit` syscall.
            return -ECHILD;
        }
        if (ptrace(PTRACE_GETREGS, thread->base.nativePid, 0, &regs) < 0) {
            error("ptrace: %s", g_strerror(errno));
            abort();
        }
        debug("threadptrace_nativeSyscall regs: rip=%llx rax=%llx arg0=%llx arg1=%llx "
              "arg2=%llx arg3=%llx arg4=%llx arg5=%llx",
              regs.rip, regs.rax, regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
    } while (regs.rip == syscall_rip);

    debug("Native syscall result %lld (%s)", regs.rax, strerror(-regs.rax));

    return regs.rax;
}

Thread* threadptrace_new(Host* host, Process* process, gint threadID) {
    ThreadPtrace* thread = g_new(ThreadPtrace, 1);

    *thread = (ThreadPtrace){
        .base = thread_create(host, process, THREADPTRACE_TYPE_ID,
                              (ThreadMethods){
                                  .run = threadptrace_run,
                                  .resume = threadptrace_resume,
                                  .terminate = threadptrace_terminate,
                                  .getReturnCode = threadptrace_getReturnCode,
                                  .isRunning = threadptrace_isRunning,
                                  .free = threadptrace_free,
                                  .newClonedPtr = threadptrace_newClonedPtr,
                                  .releaseClonedPtr = threadptrace_releaseClonedPtr,
                                  .getReadablePtr = threadptrace_getReadablePtr,
                                  .getReadableString = threadptrace_getReadableString,
                                  .getWriteablePtr = threadptrace_getWriteablePtr,
                                  .getMutablePtr = threadptrace_getMutablePtr,
                                  .flushPtrs = threadptrace_flushPtrs,
                                  .nativeSyscall = threadptrace_nativeSyscall,
                              }),
        // FIXME: This should the emulated CPU's frequency
        .tsc = {.cyclesPerSecond = 2000000000UL},
        .childState = THREAD_PTRACE_CHILD_STATE_NONE,
    };
    thread->sys = syscallhandler_new(host, process, _threadPtraceToThread(thread));

    thread->pendingWrites = g_array_new(FALSE, FALSE, sizeof(PendingWrite));
    thread->readPointers = g_array_new(FALSE, FALSE, sizeof(void*));

    worker_countObject(OBJECT_TYPE_THREAD_PTRACE, COUNTER_TYPE_NEW);
    return _threadPtraceToThread(thread);
}
