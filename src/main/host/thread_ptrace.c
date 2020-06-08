#include "main/host/thread_ptrace.h"

#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <x86intrin.h>

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

typedef enum {
    // Doesn't exist yet.
    THREAD_PTRACE_CHILD_STATE_NONE = 0,
    // Waiting for initial ptrace call.
    THREAD_PTRACE_CHILD_STATE_TRACE_ME,
    THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE,
    THREAD_PTRACE_CHILD_STATE_SYSCALL_POST,
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
    if (WIFSIGNALED(wstatus)) {
        return (StopReason){
            .type = STOPREASON_EXITED_SIGNAL,
            .exited_signal.signal = WTERMSIG(wstatus),
        };
    } else if (WIFEXITED(wstatus)) {
        return (StopReason){
            .type = STOPREASON_EXITED_NORMAL,
            .exited_normal.exit_code = WEXITSTATUS(wstatus),
        };
    } else if (WIFSTOPPED(wstatus)) {
        const int signal = WSTOPSIG(wstatus);
        if (signal == (SIGTRAP | 0x80)) {
            // See PTRACE_O_TRACESYSGOOD in ptrace(2).
            return (StopReason){
                .type = STOPREASON_SYSCALL,
            };
        } else if (wstatus >> 8 == (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
            // See PTRACE_O_TRACEEXEC in ptrace(2).
            return (StopReason){
                .type = STOPREASON_EXEC,
            };
        } else {
            return (StopReason){
                .type = STOPREASON_SIGNAL,
                .signal.signal = WSTOPSIG(wstatus),
            };
        }
    } else if (WIFCONTINUED(wstatus)) {
        return (StopReason){
            .type = STOPREASON_CONTINUED,
        };
    } else {
        return (StopReason){
            .type = STOPREASON_UNKNOWN,
        };
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

    pid_t childPID;

    int threadID;

    // Reason for the most recent transfer of control back to Shadow.
    ThreadPtraceChildState childState;

    int returnCode;

    // use for both PRE_SYSCALL and POST_SYSCALL
    struct {
        struct user_regs_struct regs;
        SysCallReturn sysCallReturn;
    } syscall;

    // Whenever we use ptrace to continue we may raise a signal.  Currently we
    // only use this to allow a signal that was already raise (e.g. SIGSEGV) to
    // be delivered.
    intptr_t signalToDeliver;
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
    pid_t pid = fork();

    switch (pid) {
        case -1: {
            error("fork: %s", g_strerror(errno));
            return -1;
        }
        case 0: {
            // child
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
            while (1) {
            } // here for compiler optimization
        }
        default: {
            // parent
            info("started process %s with PID %d", file, pid);
            return pid;
        }
    }
}

static void _threadptrace_enterStateTraceMe(ThreadPtrace* thread) {
    // PTRACE_O_EXITKILL: Kill child if our process dies.
    // PTRACE_O_TRACESYSGOOD: Handle syscall stops explicitly.
    // PTRACE_O_TRACEEXEC: Handle execve stops explicitly.
    if (ptrace(PTRACE_SETOPTIONS, thread->childPID, 0,
               PTRACE_O_EXITKILL | PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC) <
        0) {
        error("ptrace: %s", strerror(errno));
        return;
    }
    // Get a handle to the child's memory.
    char path[64];
    snprintf(path, 64, "/proc/%d/mem", thread->childPID);
    thread->childMemFile = fopen(path, "r+");
    if (thread->childMemFile == NULL) {
        error("fopen %s: %s", path, g_strerror(errno));
        return;
    }
}

static void _threadptrace_enterStateSyscallPre(ThreadPtrace* thread) {
    struct user_regs_struct* const regs = &thread->syscall.regs;
    if (ptrace(PTRACE_GETREGS, thread->childPID, 0, regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        return;
    }
}

static void _threadptrace_enterStateExecve(ThreadPtrace* thread) {
    // We have to reopen the handle to child's memory.
    char path[64];
    snprintf(path, 64, "/proc/%d/mem", thread->childPID);
    thread->childMemFile = freopen(path, "r+", thread->childMemFile);
    if (thread->childMemFile == NULL) {
        error("fopen %s: %s", path, g_strerror(errno));
        return;
    }
}

static void _threadptrace_enterStateSyscallPost(ThreadPtrace* thread) {
    switch (thread->syscall.sysCallReturn.state) {
        case SYSCALL_BLOCK: utility_assert(false); return;
        case SYSCALL_DONE:
            // Return the specified result.
            thread->syscall.regs.rax =
                thread->syscall.sysCallReturn.retval.as_u64;
            if (ptrace(PTRACE_SETREGS, thread->childPID, 0,
                       &thread->syscall.regs) < 0) {
                error("ptrace: %s", g_strerror(errno));
                return;
            }
            break;
        case SYSCALL_NATIVE:
            // Nothing to do. Just let it continue normally.
            break;
    }
}

static void _threadptrace_enterStateSignalled(ThreadPtrace* thread,
                                              int signal) {
    thread->childState = THREAD_PTRACE_CHILD_STATE_SIGNALLED;
    if (signal == SIGSEGV) {
        struct user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, thread->childPID, 0, &regs) < 0) {
            error("ptrace: %s", g_strerror(errno));
            return;
        }
        uint64_t eip = regs.rip;
        const uint8_t* buf = thread_getReadablePtr(
            _threadPtraceToThread(thread), (PluginPtr){eip}, 16);
        if (isRdtsc(buf)) {
            Tsc_emulateRdtsc(&thread->tsc,
                             &regs,
                             worker_getCurrentTime() / SIMTIME_ONE_NANOSECOND);
            if (ptrace(PTRACE_SETREGS, thread->childPID, 0, &regs) < 0) {
                error("ptrace: %s", g_strerror(errno));
                return;
            }
            return;
        }
        if (isRdtscp(buf)) {
            Tsc_emulateRdtscp(&thread->tsc,
                              &regs,
                              worker_getCurrentTime() / SIMTIME_ONE_NANOSECOND);
            if (ptrace(PTRACE_SETREGS, thread->childPID, 0, &regs) < 0) {
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

static void _threadptrace_nextChildState(ThreadPtrace* thread) {
    // Wait for child to stop.
    int wstatus;
    if (waitpid(thread->childPID, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        return;
    }
    StopReason reason = _getStopReason(wstatus);

    switch(reason.type) {
        case STOPREASON_EXITED_SIGNAL:
            debug("child %d terminated by signal %d", thread->childPID,
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
            if (thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE ||
                thread->childState == THREAD_PTRACE_CHILD_STATE_EXECVE) {
                thread->childState = THREAD_PTRACE_CHILD_STATE_SYSCALL_POST;
                _threadptrace_enterStateSyscallPost(thread);
            } else {
                thread->childState = THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE;
                _threadptrace_enterStateSyscallPre(thread);
            }
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
        default:
            error("Unhandled stop reason. wstatus: %x. stop type: %d", wstatus, reason.type);
            return;
    }
}

void threadptrace_run(Thread* base, gchar** argv, gchar** envv) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    thread->childPID = _threadptrace_fork_exec(argv[0], argv, envv);

    _threadptrace_nextChildState(thread);
    thread_resume(_threadPtraceToThread(thread));
}

static void _threadptrace_handleSyscall(ThreadPtrace* thread) {
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE);
    struct user_regs_struct* const regs = &thread->syscall.regs;

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

void threadptrace_resume(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    while (true) {
        switch (thread->childState) {
            case THREAD_PTRACE_CHILD_STATE_NONE:
                debug("THREAD_PTRACE_CHILD_STATE_NONE");
                utility_assert(false);
                break;
            case THREAD_PTRACE_CHILD_STATE_TRACE_ME:
                debug("THREAD_PTRACE_CHILD_STATE_TRACE_ME");
                break;
            case THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE:
                debug("THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE");

                // Ask the syscall handler to handle it.
                _threadptrace_handleSyscall(thread);

                switch (thread->syscall.sysCallReturn.state) {
                    case SYSCALL_BLOCK: return;
                    case SYSCALL_DONE:
                        // We have to let the child make *a* syscall, so we
                        // ensure that it will fail.
                        thread->syscall.regs.orig_rax = -1;
                        if (ptrace(PTRACE_SETREGS, thread->childPID, 0,
                                   &thread->syscall.regs) != 0) {
                            error("ptrace: %s", g_strerror(errno));
                            return;
                        }
                        break;
                    case SYSCALL_NATIVE: {
                        // Nothing to do. Just let it continue normally.
                        break;
                    }
                }
                break;
            case THREAD_PTRACE_CHILD_STATE_SYSCALL_POST:
                debug("THREAD_PTRACE_CHILD_STATE_SYSCALL_POST");
                break;
            case THREAD_PTRACE_CHILD_STATE_EXECVE:
                debug("THREAD_PTRACE_CHILD_STATE_EXECVE");
                break;
            case THREAD_PTRACE_CHILD_STATE_EXITED:
                debug("THREAD_PTRACE_CHILD_STATE_EXITED");
                return;
            case THREAD_PTRACE_CHILD_STATE_SIGNALLED:
                debug("THREAD_PTRACE_CHILD_STATE_SIGNALLED");
                break;
                // no default
        }
        _threadptrace_flushPtrs(thread);
        // Allow child to start executing.
        if (ptrace(PTRACE_SYSCALL, thread->childPID, 0,
                   thread->signalToDeliver) < 0) {
            error("ptrace %d: %s", thread->childPID, g_strerror(errno));
            return;
        }
        thread->signalToDeliver = 0;
        _threadptrace_nextChildState(thread);
    }
}

gboolean threadptrace_isRunning(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    switch (thread->childState) {
        case THREAD_PTRACE_CHILD_STATE_TRACE_ME:
        case THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE:
        case THREAD_PTRACE_CHILD_STATE_SYSCALL_POST:
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

    int status = 0;

    utility_assert(thread->childPID > 0);

    pid_t rc = waitpid(thread->childPID, &status, WNOHANG);
    utility_assert(rc != -1);

    if (rc == 0) { // child is running, request a stop
        debug("sending SIGTERM to %d", thread->childPID);
        kill(thread->childPID, SIGTERM);
        _threadptrace_nextChildState(thread);
        utility_assert(!threadptrace_isRunning(base));
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

    MAGIC_CLEAR(base);
    g_free(thread);
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
    // TODO: Use mmap instead.

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
    // TODO: Use mmap instead.

    ThreadPtrace* thread = _threadToThreadPtrace(base);
    void* rv = g_new(void, n);
    PendingWrite pendingWrite = {.pluginPtr = plugin_src, .ptr = rv, .n = n};
    g_array_append_val(thread->pendingWrites, pendingWrite);
    return rv;
}

long threadptrace_nativeSyscall(Thread* base, long n, va_list args) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    // Unimplemented for other states.
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE);
    // The last ptrace stop was just before executing a syscall instruction.
    // We'll use that to execute the desired syscall, and then restore the
    // original state.

    // Inject the requested syscall number and arguments.
    struct user_regs_struct regs = thread->syscall.regs;
    regs.orig_rax = n;
    regs.rdi = va_arg(args, long);
    regs.rsi = va_arg(args, long);
    regs.rdx = va_arg(args, long);
    regs.r10 = va_arg(args, long);
    regs.r8 = va_arg(args, long);
    regs.r9 = va_arg(args, long);
    if (ptrace(PTRACE_SETREGS, thread->childPID, 0, &regs) <
        0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }

    // Allow the current syscall to complete.
    if (ptrace(PTRACE_SYSCALL, thread->childPID, 0, 0) < 0) {
        error("ptrace %d: %s", thread->childPID, g_strerror(errno));
        abort();
    }
    int wstatus;
    if (waitpid(thread->childPID, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        abort();
    }
    StopReason reason = _getStopReason(wstatus);
    if (reason.type != STOPREASON_SYSCALL) {
        error("Unexpected stop reason type: %d, wstatus: %d", reason.type,
              wstatus);
        abort();
    }

    // Get the result.
    if (ptrace(PTRACE_GETREGS, thread->childPID, 0, &regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }
    long syscall_result = regs.rax;

    // Restore the original registers, rewinding the instruction pointer so that
    // we'll re-execute the original syscall.
    regs = thread->syscall.regs;
    regs.rip -= 2;  // Size of the syscall instruction.
    if (ptrace(PTRACE_SETREGS, thread->childPID, 0, &regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }

    // Continue, putting us back in the original pre-syscall state.
    if (ptrace(PTRACE_SYSCALL, thread->childPID, 0, 0) < 0) {
        error("ptrace %d: %s", thread->childPID, g_strerror(errno));
        abort();
    }
    if (waitpid(thread->childPID, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        abort();
    }
    reason = _getStopReason(wstatus);
    if (reason.type != STOPREASON_SYSCALL) {
        error("Unexpected stop reason type: %d, wstatus: %d", reason.type,
              wstatus);
        abort();
    }

    return syscall_result;
}

Thread* threadptrace_new(Host* host, Process* process, gint threadID) {
    ThreadPtrace* thread = g_new(ThreadPtrace, 1);

    *thread = (ThreadPtrace){
        .base = (Thread){.run = threadptrace_run,
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
                         .flushPtrs = threadptrace_flushPtrs,
                         .nativeSyscall = threadptrace_nativeSyscall,

                         .type_id = THREADPTRACE_TYPE_ID,
                         .referenceCount = 1},
        // FIXME: This should the emulated CPU's frequency
        .tsc = {.cyclesPerSecond = 2000000000UL},
        .threadID = threadID,
        .childState = THREAD_PTRACE_CHILD_STATE_NONE};

    MAGIC_INIT(&thread->base);

    thread->sys =
        syscallhandler_new(host, process, _threadPtraceToThread(thread));
    thread->pendingWrites = g_array_new(FALSE, FALSE, sizeof(PendingWrite));
    thread->readPointers = g_array_new(FALSE, FALSE, sizeof(void*));

    worker_countObject(OBJECT_TYPE_THREAD_PTRACE, COUNTER_TYPE_NEW);
    return _threadPtraceToThread(thread);
}
