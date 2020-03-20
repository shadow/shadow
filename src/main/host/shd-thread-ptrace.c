#include "main/host/shd-thread-ptrace.h"

#include <errno.h>
#include <inttypes.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <x86intrin.h>

#include "main/core/worker.h"
#include "main/host/shd-thread-protected.h"
#include "support/logger/logger.h"

#define THREADPTRACE_TYPE_ID 3024

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

typedef struct _ThreadPtrace {
    Thread base;

    SysCallHandler* sys;

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
            error("fork failed");
            return -1;
        }
        case 0: {
            // child
            // Disable RDTSC
            // FIXME: We eventually want to do this, but the emulation isn't
            // working reliably yet.
            //            if (prctl(PR_SET_TSC, PR_TSC_SIGSEGV, 0, 0, 0) < 0) {
            //                error("prctl: %s", strerror(errno));
            //                return -1;
            //            }
            // Allow parent to trace.
            if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
                error("ptrace: %s", strerror(errno));
                return -1;
            }
            // Wait for parent to attach.
            if (raise(SIGSTOP) < 0) {
                error("raise: %s", strerror(errno));
                return -1;
            }
            if (execvpe(file, argv, envp) < 0) {
                error("execvpe: %s", strerror(errno));
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
        error("fopen %s: %s", path, strerror(errno));
        return;
    }
}

static void _threadptrace_enterStateSyscallPre(ThreadPtrace* thread) {
    struct user_regs_struct* const regs = &thread->syscall.regs;
    if (ptrace(PTRACE_GETREGS, thread->childPID, 0, regs) < 0) {
        error("ptrace");
        return;
    }
    SysCallArgs args = {
        .number = regs->orig_rax,
        .args = {regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8},
    };
    thread->syscall.sysCallReturn = syscallhandler_make_syscall(
        thread->sys, _threadPtraceToThread(thread), &args);
}

static void _threadptrace_enterStateExecve(ThreadPtrace* thread) {
    // We have to reopen the handle to child's memory.
    char path[64];
    snprintf(path, 64, "/proc/%d/mem", thread->childPID);
    thread->childMemFile = freopen(path, "r+", thread->childMemFile);
    if (thread->childMemFile == NULL) {
        error("fopen %s: %s", path, strerror(errno));
        return;
    }
}

static void _threadptrace_enterStateSyscallPost(ThreadPtrace* thread) {
    switch (thread->syscall.sysCallReturn.state) {
        case SYSCALL_RETURN_BLOCKED: utility_assert(false); return;
        case SYSCALL_RETURN_DONE:
            // Return the specified result.
            thread->syscall.regs.rax =
                thread->syscall.sysCallReturn.retval.as_u64;
            if (ptrace(PTRACE_SETREGS, thread->childPID, 0,
                       &thread->syscall.regs) < 0) {
                error("ptrace");
                return;
            }
            break;
        case SYSCALL_RETURN_NATIVE:
            // Nothing to do. Just let it continue normally.
            break;
    }
}

static uint64_t _get_cycles_per_ms() {
    // FIXME: use cpuid to get the canonical answer.
    uint64_t min_cycles = UINT64_MAX;
    for (int i = 0; i < 10; ++i) {
        struct timespec ts_start;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        uint64_t rdtsc_start = __rdtsc();

        usleep(10000);

        struct timespec ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        uint64_t rdtsc_end = __rdtsc();
        uint64_t cycles = rdtsc_end - rdtsc_start;
        uint64_t ns = (uint64_t)ts_end.tv_sec * 1000000000UL + ts_end.tv_nsec -
                      (uint64_t)ts_start.tv_sec * 1000000000UL -
                      ts_start.tv_nsec;
        min_cycles = MIN(min_cycles, cycles * 1000000 / ns);
    }
    info("Calculated %" PRIu64 " cycles_per_ms", min_cycles);
    return min_cycles;
}

static void _set_rdtsc_cycles(struct user_regs_struct* regs) {
    // FIXME this needs a mutex to handle multiple worker threads
    static uint64_t cycles_per_ms = 0;
    if (cycles_per_ms == 0) {
        cycles_per_ms = _get_cycles_per_ms();
    }

    // RDTSC instruction, which we disabled after fork. emulate it.
    EmulatedTime t = worker_getEmulatedTime();

    uint64_t cycles = t / SIMTIME_ONE_MILLISECOND * cycles_per_ms;
    regs->rdx = (cycles >> 32) & 0xffffffff;
    regs->rax = cycles & 0xffffffff;
}

static void _emulate_rdtsc(struct user_regs_struct* regs) {
    _set_rdtsc_cycles(regs);
    regs->rip += 2;
}

static void _emulate_rdtscp(struct user_regs_struct* regs) {
    _set_rdtsc_cycles(regs);
    // FIXME: using the real instruction to put plausible data int rcx, but we
    // probably want an emulated value. It's some metadata about the processor,
    // including the processor ID.
    unsigned int a;
    __rdtscp(&a);
    regs->rcx = a;
    regs->rip += 3;
}

static bool _is_rdtsc(uint8_t* buf) { return buf[0] == 0x0f && buf[1] == 0x31; }

static bool _is_rdtscp(uint8_t* buf) {
    return buf[0] == 0x0f && buf[1] == 0x01 && buf[2] == 0xf9;
}

static void _threadptrace_enterStateSignalled(ThreadPtrace* thread,
                                              int signal) {
    thread->childState = THREAD_PTRACE_CHILD_STATE_SIGNALLED;
    if (signal == SIGSEGV) {
        struct user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, thread->childPID, 0, &regs) < 0) {
            error("ptrace");
            return;
        }
        uint8_t buf[16];
        uint64_t eip = regs.rip;
        thread_memcpyToShadow(_threadPtraceToThread(thread), buf,
                              (PluginPtr){eip}, 16);
        if (_is_rdtsc(buf)) {
            _emulate_rdtsc(&regs);
            if (ptrace(PTRACE_SETREGS, thread->childPID, 0, &regs) < 0) {
                error("ptrace");
                return;
            }
            return;
        }
        if (_is_rdtscp(buf)) {
            _emulate_rdtscp(&regs);
            if (ptrace(PTRACE_SETREGS, thread->childPID, 0, &regs) < 0) {
                error("ptrace");
                return;
            }
            return;
        }
        error("Unhandled SIGSEGV addr:%016lx contents:%x %x %x %x %x %x %x %x",
              eip, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
              buf[7]);
        // fall through
    }
    // Deliver the signal.
    thread->signalToDeliver = signal;
}

static void _threadptrace_nextChildState(ThreadPtrace* thread) {
    // Wait for child to stop.
    int wstatus;
    if (waitpid(thread->childPID, &wstatus, 0) < 0) {
        error("waitpid: %s", strerror(errno));
        return;
    }

    if (WIFSIGNALED(wstatus)) {
        // Killed by a signal.
        int signum = WTERMSIG(wstatus);
        debug("child %d terminated by signal %d", thread->childPID, signum);
        thread->childState = THREAD_PTRACE_CHILD_STATE_EXITED;
        thread->returnCode = -1;
    }
    if (WIFEXITED(wstatus)) {
        // Exited for some other reason.
        thread->childState = THREAD_PTRACE_CHILD_STATE_EXITED;
        thread->returnCode = WEXITSTATUS(wstatus);
        return;
    }
    if (!WIFSTOPPED(wstatus)) {
        // NOT stopped by a ptrace event.
        error("Unknown waitpid reason");
        return;
    }
    const int signal = WSTOPSIG(wstatus);
    if (signal == SIGSTOP &&
        thread->childState == THREAD_PTRACE_CHILD_STATE_NONE) {
        // We caught the "raise(SIGSTOP)" just after forking.
        thread->childState = THREAD_PTRACE_CHILD_STATE_TRACE_ME;
        _threadptrace_enterStateTraceMe(thread);
        return;
    }
    // Condition taken from 'man ptrace' for PTRACE_O_TRACEEXEC
    if (wstatus >> 8 == (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
        thread->childState = THREAD_PTRACE_CHILD_STATE_EXECVE;
        _threadptrace_enterStateExecve(thread);
        return;
    }
    // See PTRACE_O_TRACESYSGOOD in `man 2 ptrace`
    if (signal == (SIGTRAP | 0x80)) {
        if (thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE ||
            thread->childState == THREAD_PTRACE_CHILD_STATE_EXECVE) {
            thread->childState = THREAD_PTRACE_CHILD_STATE_SYSCALL_POST;
            _threadptrace_enterStateSyscallPost(thread);
        } else {
            thread->childState = THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE;
            _threadptrace_enterStateSyscallPre(thread);
        }
        return;
    }
    _threadptrace_enterStateSignalled(thread, signal);

    error("Unhandled stop signal: %d", signal);
    return;
}

void threadptrace_run(Thread* base, gchar** argv, gchar** envv) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    thread->childPID = _threadptrace_fork_exec(argv[0], argv, envv);

    _threadptrace_nextChildState(thread);
    thread_resume(_threadPtraceToThread(thread));
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
                switch (thread->syscall.sysCallReturn.state) {
                    case SYSCALL_RETURN_BLOCKED: return;
                    case SYSCALL_RETURN_DONE:
                        // We have to let the child make *a* syscall, so we
                        // ensure that it will fail.
                        thread->syscall.regs.orig_rax = -1;
                        if (ptrace(PTRACE_SETREGS, thread->childPID, 0,
                                   &thread->syscall.regs) != 0) {
                            error("ptrace");
                            return;
                        }
                        break;
                    case SYSCALL_RETURN_NATIVE: {
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
                return;
                // no default
        }
        // Allow child to start executing.
        if (ptrace(PTRACE_SYSCALL, thread->childPID, 0,
                   thread->signalToDeliver) < 0) {
            error("ptrace %d: %s", thread->childPID, strerror(errno));
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

void threadptrace_setSysCallResult(Thread* base, SysCallReg retval) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL_PRE);
    thread->syscall.sysCallReturn =
        (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval = retval};
}

void threadptrace_free(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    if (thread->sys) {
        syscallhandler_unref(thread->sys);
    }

    MAGIC_CLEAR(base);
    g_free(thread);
}

void threadptrace_memcpyToShadow(Thread* base, void* shadow_dst,
                                 PluginPtr plugin_src, size_t n) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    clearerr(thread->childMemFile);
    if (fseek(thread->childMemFile, plugin_src.val, SEEK_SET) < 0) {
        error("fseek");
        return;
    }
    size_t count = fread(shadow_dst, 1, n, thread->childMemFile);
    if (count != n) {
        if (feof(thread->childMemFile)) {
            error("EOF");
            return;
        }
        error("fread returned %d instead of %d: %s", count, n,
              ferror(thread->childMemFile));
    }
    return;
}

void threadptrace_memcpyToPlugin(Thread* base, PluginPtr plugin_dst,
                                 void* shadow_src, size_t n) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    if (fseek(thread->childMemFile, plugin_dst.val, SEEK_SET) < 0) {
        error("fseek");
        return;
    }
    size_t count = fwrite(shadow_src, 1, n, thread->childMemFile);
    if (count != n) {
        error("fread");
    }
    // Consider removing? Or putting in a separate Thread API to minimize
    // syncs if there are multiple writes?
    if (fflush(thread->childMemFile) != 0) {
        error("fflush");
    }
    return;
}

Thread* threadptrace_new(gint threadID, SysCallHandler* sys) {
    ThreadPtrace* thread = g_new(ThreadPtrace, 1);
    *thread = (ThreadPtrace){
        .base = (Thread){.run = threadptrace_run,
                         .resume = threadptrace_resume,
                         .terminate = threadptrace_terminate,
                         .setSysCallResult = threadptrace_setSysCallResult,
                         .getReturnCode = threadptrace_getReturnCode,
                         .isRunning = threadptrace_isRunning,
                         .free = threadptrace_free,
                         .memcpyToPlugin = threadptrace_memcpyToPlugin,
                         .memcpyToShadow = threadptrace_memcpyToShadow,

                         .type_id = THREADPTRACE_TYPE_ID,
                         .referenceCount = 1},
        .sys = sys,
        .threadID = threadID,
        .childState = THREAD_PTRACE_CHILD_STATE_NONE};

    syscallhandler_ref(sys);
    MAGIC_INIT(&thread->base);

    return _threadPtraceToThread(thread);
}
