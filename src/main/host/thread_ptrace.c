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
#include "shim/ipc.h"
#include "support/logger/logger.h"

#define THREADPTRACE_TYPE_ID 3024

// glibc in centos 7 does not include the following, but it does use a supported
// kernel (3.10 > 3.8)
#if !defined(PTRACE_O_EXITKILL)
#define PTRACE_O_EXITKILL (1 << 20)
#endif

// Using PTRACE_O_TRACECLONE causes the `clone` syscall to fail on Ubuntu 18.04.
// We instead add the CLONE_PTRACE flag to the clone syscall itself.
#define THREADPTRACE_PTRACE_OPTIONS (PTRACE_O_EXITKILL | PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC)

static char SYSCALL_INSTRUCTION[] = {0x0f, 0x05};

// Number of times to do a non-blocking wait while waiting for traced thread.
#define THREADPTRACE_MAX_SPIN 8096

typedef enum {
    // Doesn't exist yet.
    THREAD_PTRACE_CHILD_STATE_NONE = 0,
    // Waiting for initial ptrace call.
    THREAD_PTRACE_CHILD_STATE_TRACE_ME,
    // In a syscall ptrace stop.
    THREAD_PTRACE_CHILD_STATE_SYSCALL,
    // Handling a syscall via IPC. Child thread should be spinning. While in
    // this state we may have to handle syscall ptrace-stops, which we should
    // allow to execute natively without moving out of this state.
    THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL,
    // In an execve stop.
    THREAD_PTRACE_CHILD_STATE_EXECVE,
    // In a signal stop.
    THREAD_PTRACE_CHILD_STATE_SIGNALLED,
    // Exited.
    THREAD_PTRACE_CHILD_STATE_EXITED,
} ThreadPtraceChildState;

typedef struct _PendingWrite {
    PluginPtr pluginPtr;
    void* ptr;
    size_t n;
} PendingWrite;

typedef enum {
    STOPREASON_EXITED_NORMAL,
    STOPREASON_EXITED_SIGNAL,
    STOPREASON_SIGNAL,
    STOPREASON_SYSCALL,
    STOPREASON_SHIM_EVENT,
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
        ShimEvent shim_event;
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

    // Reason for the most recent transfer of control back to Shadow.
    ThreadPtraceChildState childState;

    int returnCode;

    // Pointer to *some* syscall instruction, for when we need to force the
    // child process to make a syscall. In particular this is useful when we
    // need the plugin to make a syscall, and aren't in a ptrace syscall stop.
    intptr_t syscall_rip;

    struct {
        struct user_regs_struct value;
        // Whether `value` holds the values that the CPU registers ought to
        // have before returning control to the plugin.
        bool valid;
        // Whether `value` needs to be written back.
        bool dirty;
    } regs;

    SysCallArgs syscall_args;

    // use for IPC_SYSCALL
    struct {
        // While handling a syscall via IPC, sometimes we need to stop the
        // child process to perform ptrace-operations. Tracks whether we've
        // done so.
        bool stopped;

        // When a ptrace-stop that would otherwise change the state of the
        // child happens while processing an IPC request, we buffer it here to
        // be processed after the IPC request is completed.
        bool havePendingStop;
        StopReason pendingStop;
    } ipc_syscall;

    // Whenever we use ptrace to continue we may raise a signal.  Currently we
    // only use this to allow a signal that was already raise (e.g. SIGSEGV) to
    // be delivered.
    intptr_t signalToDeliver;

    // True if we have detached ptrace from the plugin and should attach before
    // executing another ptrace operation.
    bool needAttachment;

    // Shared memory block for fast communication with plugin process.
    // Intentionally opaque; details are implementation details of ShimEvent.
    ShMemBlock ipcBlk;

    // Data shared with the shim.
    ShimSharedMem* shimSharedMem;

    // Shared memory handle for `shimSharedMem`.
    ShMemBlock shimSharedMemBlock;

    // Enable syscall handling via IPC.
    bool enableIpc;
} ThreadPtrace;

// Forward declaration.
static void _threadptrace_memcpyToPlugin(ThreadPtrace* thread,
                                         PluginPtr plugin_dst, void* shadow_src,
                                         size_t n);
const void* threadptrace_getReadablePtr(Thread* base, PluginPtr plugin_src,
                                        size_t n);
static void _threadptrace_ensureStopped(ThreadPtrace *thread);

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
    if (ptrace(PTRACE_SETOPTIONS, thread->base.nativeTid, 0, THREADPTRACE_PTRACE_OPTIONS) < 0) {
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

    // Previous cached address is no longer valid.
    thread->syscall_rip = 0;
}

static void _threadptrace_enterStateSyscall(ThreadPtrace* thread) {
    struct user_regs_struct* regs = &thread->regs.value;
    if (!thread->regs.valid) {
        if (ptrace(PTRACE_GETREGS, thread->base.nativeTid, 0, regs) < 0) {
            error("ptrace: %s", g_strerror(errno));
            return;
        }
        thread->regs.value.rax = thread->regs.value.orig_rax;
        thread->regs.valid = true;
        thread->syscall_rip = regs->rip - sizeof(SYSCALL_INSTRUCTION);
    }
    thread->syscall_args = (SysCallArgs){
        .number = regs->orig_rax,
        .args = {regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8, regs->r9},
    };
}

static void _threadptrace_enterStateIpcSyscall(ThreadPtrace* thread, ShimEvent* event) {
    debug("enterStateIpcSyscall");
    thread->childState = THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL;
    thread->syscall_args = event->event_data.syscall.syscall_args;
}

static void _threadptrace_enterStateSignalled(ThreadPtrace* thread,
                                              int signal) {
    thread->childState = THREAD_PTRACE_CHILD_STATE_SIGNALLED;
    if (signal == SIGSEGV) {
        struct user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, thread->base.nativeTid, 0, &regs) < 0) {
            error("ptrace: %s", g_strerror(errno));
            return;
        }
        debug("threadptrace_enterStateSignalled regs: rip=%llx rax=%llx arg0=%llx arg1=%llx "
              "arg2=%llx arg3=%llx arg4=%llx arg5=%llx",
              regs.rip, regs.rax, regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
        uint64_t eip = regs.rip;
        const uint8_t* buf = process_getReadablePtr(
            thread->base.process, _threadPtraceToThread(thread), (PluginPtr){eip}, 16);
        if (isRdtsc(buf)) {
            debug("emulating rdtsc");
            Tsc_emulateRdtsc(&thread->tsc, &regs, worker_getCurrentTime() / SIMTIME_ONE_NANOSECOND);
            if (ptrace(PTRACE_SETREGS, thread->base.nativeTid, 0, &regs) < 0) {
                error("ptrace: %s", g_strerror(errno));
                return;
            }
            return;
        }
        if (isRdtscp(buf)) {
            debug("emulating rdtscp");
            Tsc_emulateRdtscp(
                &thread->tsc, &regs, worker_getCurrentTime() / SIMTIME_ONE_NANOSECOND);
            if (ptrace(PTRACE_SETREGS, thread->base.nativeTid, 0, &regs) < 0) {
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
    } else if (signal == SIGSTOP) {
        debug("Suppressing SIGSTOP");
        // We send SIGSTOP to the child when we need to stop it or detach from
        // it, but sometimes it ends up stopping for another reason first (e.g.
        // a syscall). After resuming the child later, we get a SIGSTOP event,
        // which we no longer want to deliver to the child.
        return;
    }
    // Deliver the signal.
    warning("Delivering signal %d", signal);
    thread->signalToDeliver = signal;
}

static void _threadptrace_updateChildState(ThreadPtrace* thread, StopReason reason) {
    switch (reason.type) {
        case STOPREASON_EXITED_SIGNAL:
            debug("child %d terminated by signal %d", thread->base.nativeTid,
                  reason.exited_signal.signal);
            thread->childState = THREAD_PTRACE_CHILD_STATE_EXITED;
            thread->returnCode = return_code_for_signal(reason.exited_signal.signal);
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
        case STOPREASON_SHIM_EVENT:
            utility_assert(reason.shim_event.event_id == SHD_SHIM_EVENT_SYSCALL);
            _threadptrace_enterStateIpcSyscall(thread, &reason.shim_event);
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
        case STOPREASON_UNKNOWN:
        case STOPREASON_CONTINUED:
            error("Unhandled stop reason. stop type: %d", reason.type);
            return;
    }
    error("Invalid stop reason. stop type: %d", reason.type);
    return;
}

static pid_t _waitpid_spin(pid_t pid, int *wstatus, int options) {
    int count = 0;
    pid_t rv = 0;
    // First do non-blocking waits.
    do {
        rv = waitpid(pid, wstatus, options|WNOHANG);
    } while (rv == 0 && count++ < THREADPTRACE_MAX_SPIN);

    // If we haven't gotten an answer yet, make a blocking call.
    if (rv == 0) {
        rv = waitpid(pid, wstatus, options);
    }

    return rv;
}

// Waits for a ptrace or shim event.
static StopReason _threadptrace_hybridSpin(ThreadPtrace* thread) {
    // Only used for an shim-event-stop; lifted out of the loop so we don't
    // re-initialize on every iteration.
    StopReason event_stop = {
        .type = STOPREASON_SHIM_EVENT,
    };
    while (1) {
        if (thread->enableIpc &&
            shimevent_tryRecvEventFromPlugin(thread->ipcBlk.p, &event_stop.shim_event) == 0) {
            debug("Got shim stop");
            return event_stop;
        }
        // TODO: We lose a bit of efficiency here due to `waitpid` being
        // substantially slower than `shimevent_tryRecvEventFromPlugin`, even
        // with `WNOHANG`.  If a shim event comes in while we're executing
        // `waitpid`, the time spent finishing that call before we check for
        // the shim event again is wasted.
        //
        // Experimentally, putting the shim recv in an inner loop to try it
        // ~2000 times between each `waitpid` seems to be a bit of an
        // improvement. This seems like a parameter that could be very
        // sensitive to the workload and simulation platform, though.
        //
        // A better approach might be to use a dedicated thread to wait on all
        // plugin threads (i.e. pass -1 as the first parameter), and push
        // events into an in-memory queue. This thread could then spin over two
        // fast in-memory queues instead of one fast queue and one slow(er)
        // syscall. Before going down that path we'd need to verify that a
        // thread other than the direct parent can receive ptrace stops this
        // way, though. According to wait(2), threads can wait on children of
        // other threads in the same thread group, but I wouldn't be surprised
        // if ptrace events are a special case.
        int wstatus;
        pid_t pid = waitpid(thread->base.nativeTid, &wstatus, WNOHANG);
        if (pid < 0) {
            error("waitpid: %s", strerror(pid));
            abort();
        }
        if (pid != 0) {
            debug("Got ptrace stop");
            StopReason ptraceStopReason = _getStopReason(wstatus);

            // Pre-emptively save registers here.  TODO: do this lazily, since
            // we won't always need them. Doing it here makes it easier to
            // ensure we get the right value for rax though, since we know what
            // kind of ptrace stop just happened.
            if (ptrace(PTRACE_GETREGS, thread->base.nativeTid, 0, &thread->regs.value) < 0) {
                error("ptrace: %s", g_strerror(errno));
                abort();
            }
            if (ptraceStopReason.type == STOPREASON_SYSCALL) {
                debug("Saving syscall rip");
                thread->regs.value.rax = thread->regs.value.orig_rax;
                thread->syscall_rip = thread->regs.value.rip - sizeof(SYSCALL_INSTRUCTION);
            }
            thread->regs.valid = true;
            thread->regs.dirty = false;

            if (thread->enableIpc &&
                shimevent_tryRecvEventFromPlugin(thread->ipcBlk.p, &event_stop.shim_event) == 0) {
                // The plugin finished sending an event after our previous
                // attempt to receive it, and then hit a ptrace-stop.  We need
                // to handle the sent-event first, and buffer the ptrace-stop
                // to be handled later.  In particular, the ptrace-stop could
                // be a blocking futex syscall on the shim IPC control
                // structures; if we try to execute it before responding to the
                // shim event, we could deadlock.
                debug("Buffering ptrace-stop while handling shim event");
                thread->ipc_syscall.stopped = true;
                thread->ipc_syscall.pendingStop = ptraceStopReason;
                thread->ipc_syscall.havePendingStop = true;
                return event_stop;
            }
            return ptraceStopReason;
        }
    };
}

static void _threadptrace_nextChildState(ThreadPtrace* thread) {
    StopReason reason = _threadptrace_hybridSpin(thread);
    _threadptrace_updateChildState(thread, reason);
}

pid_t threadptrace_run(Thread* base, gchar** argv, gchar** envv) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);

    /* set the env for the child */
    gchar** myenvv = g_strdupv(envv);

    if (thread->enableIpc) {
        ShMemBlockSerialized ipcBlkSerial = shmemallocator_globalBlockSerialize(&thread->ipcBlk);

        char ipcBlkBuf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
        shmemblockserialized_toString(&ipcBlkSerial, ipcBlkBuf);

        /* append to the env */
        myenvv = g_environ_setenv(myenvv, "_SHD_IPC_BLK", ipcBlkBuf, TRUE);
    }

    gchar* envStr = utility_strvToNewStr(myenvv);
    gchar* argStr = utility_strvToNewStr(argv);
    message("forking new thread with environment '%s' and arguments '%s'",
            envStr, argStr);
    g_free(envStr);
    g_free(argStr);

    thread->base.nativeTid = _threadptrace_fork_exec(argv[0], argv, myenvv);
    thread->base.nativePid = thread->base.nativeTid;

    if (thread->enableIpc) {
        // Send 'start' event.
        ShimEvent startEvent = {
            .event_id = SHD_SHIM_EVENT_START,
            .event_data.start = {
                .simulation_nanos = worker_getEmulatedTime(),
                .shim_shared_mem = shmemallocator_globalBlockSerialize(&thread->shimSharedMemBlock),
            }};
        shimevent_sendEventToPlugin(thread->ipcBlk.p, &startEvent);
    }

    _threadptrace_nextChildState(thread);

    return thread->base.nativePid;
}

static SysCallReturn _threadptrace_handleSyscall(ThreadPtrace* thread, SysCallArgs* args) {
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL ||
                   thread->childState == THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL);
    if(args->number == SYS_shadow_set_ptrace_allow_native_syscalls) {
        bool val = args->args[0].as_i64;
        debug("SYS_shadow_set_ptrace_allow_native_syscalls %d", val);
        thread->shimSharedMem->ptrace_allow_native_syscalls = val;
        return (SysCallReturn) {.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    if(args->number == SYS_shadow_get_ipc_blk) {
        PluginPtr ipc_blk_pptr = args->args[0].as_ptr;
        debug("SYS_shadow_get_ipc_blk %p", (void*)ipc_blk_pptr.val);
        ShMemBlockSerialized* ipc_blk_ptr = process_getWriteablePtr(
            thread->base.process, &thread->base, ipc_blk_pptr, sizeof(*ipc_blk_ptr));
        *ipc_blk_ptr = shmemallocator_globalBlockSerialize(&thread->ipcBlk);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    if (thread->enableIpc && thread->shimSharedMem->ptrace_allow_native_syscalls) {
        if (args->number == SYS_brk) {
            // brk should *always* be interposed so that the MemoryManager can track it.
            debug("Interposing brk even though native syscalls are enabled");
            return syscallhandler_make_syscall(thread->sys, args);
        } else {
            debug("Ptrace allowing native syscalls");
            return (SysCallReturn){.state = SYSCALL_NATIVE};
        }
    }
    debug("Ptrace not allowing native syscalls");

    return syscallhandler_make_syscall(thread->sys, args);
}

static void _threadptrace_flushPtrs(ThreadPtrace* thread) {
    _threadptrace_ensureStopped(thread);

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

static const char* _regs_to_str(const struct user_regs_struct* regs) {
    static char buf[1000];
    snprintf(
        buf, sizeof(buf),
        "r15:0x%llx r14: 0x%llx r13:0x%llx r12:0x%llx rbp:0x%llx rbx:0x%llx r11:0x%llx "
        "r10:0x%llx r9:0x%llx r8:0x%llx rax:0x%llx rcx:0x%llx rdx:0x%llx rsi:0x%llx rdi:0x%llx "
        "orig_rax:0x%llx rip:0x%llx cs:0x%llx eflags:0x%llx rsp:0x%llx ss:0x%llx "
        "fs_base:0x%llx gs_base:0x%llx ds:0x%llx es:0x%llx fs:0x%llx gs:0x%llx",
        regs->r15, regs->r14, regs->r13, regs->r12, regs->rbp, regs->rbx, regs->r11, regs->r10,
        regs->r9, regs->r8, regs->rax, regs->rcx, regs->rdx, regs->rsi, regs->rdi, regs->orig_rax,
        regs->rip, regs->cs, regs->eflags, regs->rsp, regs->ss, regs->fs_base, regs->gs_base,
        regs->ds, regs->es, regs->fs, regs->gs);
    return buf;
}

static void _threadptrace_doAttach(ThreadPtrace* thread) {
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL
            || thread->childState == THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL);

    debug("thread %i attaching to child %i", thread->base.tid, (int)thread->base.nativeTid);
    if (ptrace(PTRACE_ATTACH, thread->base.nativeTid, 0, 0) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }
    int wstatus;
    if (_waitpid_spin(thread->base.nativeTid, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        abort();
    }
    StopReason reason = _getStopReason(wstatus);
    utility_assert(reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGSTOP);

    if (ptrace(PTRACE_SETOPTIONS, thread->base.nativeTid, 0, THREADPTRACE_PTRACE_OPTIONS) < 0) {
        error("ptrace: %s", strerror(errno));
        return;
    }

#if DEBUG
    if (thread->regs.valid && !thread->regs.dirty) {
        // Check that rip is where we left it.
        struct user_regs_struct actual_regs;
        if (ptrace(PTRACE_GETREGS, thread->base.nativeTid, 0, &actual_regs) < 0) {
            error("ptrace: %s", g_strerror(errno));
            abort();
        }
        utility_assert(thread->regs.value.rip == actual_regs.rip);
    }
#endif

    thread->needAttachment = false;
}

static void _threadptrace_doDetach(ThreadPtrace* thread) {
    utility_assert(thread->childState == THREAD_PTRACE_CHILD_STATE_SYSCALL ||
                   thread->childState == THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL);
    if (thread->needAttachment) {
        // We're already detached.
        debug("Already detached");
        return;
    }

    _threadptrace_ensureStopped(thread);

    // Detach, delivering a sigstop.
    if (ptrace(PTRACE_DETACH, thread->base.nativeTid, 0, SIGSTOP) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }

    debug("detached");
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
                break;
            case THREAD_PTRACE_CHILD_STATE_TRACE_ME:
                debug("THREAD_PTRACE_CHILD_STATE_TRACE_ME");
                break;
            case THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL: {
                debug("THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL");
                SysCallReturn ret = syscallhandler_make_syscall(thread->sys, &thread->syscall_args);
                switch (ret.state) {
                    case SYSCALL_BLOCK:
                        debug("ipc_syscall blocked");
                        // Don't leave it spinning.
                        _threadptrace_ensureStopped(thread);
                        return ret.cond;
                    case SYSCALL_DONE: {
                        debug("ipc_syscall done");
                        ShimEvent shim_result = {
                        .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                        .event_data = {
                            .syscall_complete = {.retval = ret.retval,
                                                 .simulation_nanos =
                                                     worker_getEmulatedTime()},

                        }};
                        shimevent_sendEventToPlugin(thread->ipcBlk.p, &shim_result);
                        break;
                                       }
                    case SYSCALL_NATIVE: {
                        debug("ipc_syscall do-native");
                        SysCallArgs* args = &thread->syscall_args;
                        long rv = thread_nativeSyscall(
                            base, args->number, args->args[0], args->args[1], args->args[2],
                            args->args[3], args->args[4], args->args[5]);
                        ShimEvent shim_result = {
                        .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                        .event_data = {
                            .syscall_complete = {.retval = rv,
                                                 .simulation_nanos =
                                                     worker_getEmulatedTime()},

                        }};
                        shimevent_sendEventToPlugin(thread->ipcBlk.p, &shim_result);
                    }
                }
                if (thread->childState != THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL) {
                    if (thread->ipc_syscall.havePendingStop) {
                        // This can happen, e.g., when processing `exit_group`
                        // via a shim event. The syscall handler currently
                        // returns `SYSCALL_NATIVE`, so we ptrace-step through
                        // the syscall, causing the child to exit. The pending
                        // stop is no longer relevant (e.g. logging inside the
                        // shim).
                        debug("Dropping pending stop because of state change to %d",
                              thread->ipc_syscall.pendingStop.type);
                        thread->ipc_syscall.havePendingStop = false;
                    }
                    // Executing the syscall changed our state. We need to process it before
                    // waiting again.
                    debug("State changed to %d while processing IPC_SYSCALL; continuing",
                          thread->ipc_syscall.pendingStop.type);
                    continue;
                }
                if (thread->ipc_syscall.havePendingStop) {
                    // We hit a ptrace-stop while processing the IPC stop.
                    // Handle that now.
                    debug("Processing a pending ptrace stop");
                    _threadptrace_updateChildState(thread, thread->ipc_syscall.pendingStop);
                    thread->ipc_syscall.havePendingStop = false;
                    continue;
                }

                break;
                                                        }
            case THREAD_PTRACE_CHILD_STATE_SYSCALL:
                debug("THREAD_PTRACE_CHILD_STATE_SYSCALL");

                SysCallReturn ret = _threadptrace_handleSyscall(thread, &thread->syscall_args);

                switch (ret.state) {
                    case SYSCALL_BLOCK:
                        return ret.cond;
                    case SYSCALL_DONE:
                        // Return the specified result.
                        utility_assert(thread->regs.valid);
                        thread->regs.value.rax = ret.retval.as_u64;
                        thread->regs.dirty = true;
                        break;
                    case SYSCALL_NATIVE: {
                        // Have the plugin execute the original syscall
                        SysCallArgs* args = &thread->syscall_args;
                        thread_nativeSyscall(base, args->number, args->args[0], args->args[1],
                                             args->args[2], args->args[3], args->args[4],
                                             args->args[5]);
                        // The syscall should have left us in exactly the state from
                        // which we want to resume execution. In particular we DON'T want
                        // to restore the old instruction pointer after executing an execve syscall.
                        thread->regs.valid = false;
                        thread->regs.dirty = false;

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
        if (thread->childState != THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL ||
            thread->ipc_syscall.stopped) {

            if (thread->regs.dirty) {
                debug("Restoring registers");
                utility_assert(thread->regs.valid);
                // restore registers
                // TODO: track if dirty, and only restore if so.
                if (ptrace(PTRACE_SETREGS, thread->base.nativeTid, 0, &thread->regs.value) < 0) {
                    error("ptrace: %s", g_strerror(errno));
                    abort();
                }
                thread->regs.dirty = false;
            }
            _threadptrace_flushPtrs(thread);

            debug("ptrace resuming with signal %ld", thread->signalToDeliver);
            // Allow child to start executing.
            if (ptrace(PTRACE_SYSEMU, thread->base.nativeTid, 0, thread->signalToDeliver) < 0) {
                error("ptrace %d: %s", thread->base.nativeTid, g_strerror(errno));
                return NULL;
            }
            thread->regs.valid = false;
            thread->signalToDeliver = 0;
            thread->ipc_syscall.stopped = false;
        } else {
        }
        debug("waiting for next state");
        _threadptrace_nextChildState(thread);
    }
}

bool threadptrace_isRunning(Thread* base) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    switch (thread->childState) {
        case THREAD_PTRACE_CHILD_STATE_TRACE_ME:
        case THREAD_PTRACE_CHILD_STATE_SYSCALL:
        case THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL:
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

    // need to kill() and not ptrace() since the process may not be stopped
    if (kill(thread->base.nativeTid, SIGKILL) < 0) {
        warning("kill %d: %s", thread->base.nativePid, g_strerror(errno));
    }

    int wstatus;
    if (_waitpid_spin(thread->base.nativePid, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        return;
    }

    StopReason reason = _getStopReason(wstatus);

    if (reason.type != STOPREASON_EXITED_NORMAL && reason.type != STOPREASON_EXITED_SIGNAL) {
        error("Expected process %d to exit after SIGKILL, instead received status %d",
              thread->base.nativePid, wstatus);
    }

    _threadptrace_updateChildState(thread, reason);
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

// Ensure that the child is in a ptrace-stop. If it's not (e.g. because is it's
// spinning in its shim-event-recv loop), we force it into one.
static void _threadptrace_ensureStopped(ThreadPtrace *thread) {
    if (thread->childState != THREAD_PTRACE_CHILD_STATE_IPC_SYSCALL) {
        debug("Not in ipc_syscall; should already be stopped");
        return;
    }

    if (thread->ipc_syscall.stopped) {
        debug("In ipc_syscall; looks like already stopped");
        return;
    }

    debug("sending sigstop");
    if (syscall(SYS_tgkill, thread->base.nativePid, thread->base.nativeTid, SIGSTOP) < 0) {
        error("kill: %s", g_strerror(errno));
        abort();
    }

    utility_assert(!thread->regs.dirty);

    while(1) {
        int wstatus;
        if (_waitpid_spin(thread->base.nativeTid, &wstatus, 0) < 0) {
            error("waitpid: %s", g_strerror(errno));
            abort();
        }
        StopReason reason = _getStopReason(wstatus);
        if (reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGSTOP) {
            debug("got sigstop");
            thread->ipc_syscall.stopped = true;

            if (ptrace(PTRACE_GETREGS, thread->base.nativeTid, 0, &thread->regs.value) < 0) {
                error("ptrace: %s", g_strerror(errno));
                abort();
            }
            // Do NOT copy orig_rax to rax here; that should only be done in a syscall stop.
            thread->regs.valid = true;
            thread->regs.dirty = false;
            return;
        }
        if (reason.type == STOPREASON_SYSCALL) {
            debug("got syscall stop");
            thread->ipc_syscall.stopped = true;

            // Buffer the syscall to be processed later.
            utility_assert(!thread->ipc_syscall.havePendingStop);
            thread->ipc_syscall.pendingStop = reason;
            thread->ipc_syscall.havePendingStop = true;

            if (ptrace(PTRACE_GETREGS, thread->base.nativeTid, 0, &thread->regs.value) < 0) {
                error("ptrace: %s", g_strerror(errno));
                abort();
            }
            thread->regs.value.rax = thread->regs.value.orig_rax;
            thread->syscall_rip = thread->regs.value.rip - sizeof(SYSCALL_INSTRUCTION);
            thread->regs.valid = true;
            thread->regs.dirty = false;
            return;
        } else {
            error("Unexpected stop");
            abort();
        }
    }
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
    char* str = g_new(char, n);
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
        *strlen = count - 1;
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

static long threadptrace_nativeSyscall(Thread* base,
                                               long n,
                                               va_list args) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    debug("threadptrace_nativeSyscall %ld", n);
    _threadptrace_ensureStopped(thread);

    // The last ptrace stop was just before executing a syscall instruction.
    // We'll use that to execute the desired syscall, and then restore the
    // original state.

    // Inject the requested syscall number and arguments.
    // Set up arguments to syscall.
    utility_assert(thread->regs.valid);
    struct user_regs_struct regs = thread->regs.value;
    regs.rax = n;
    regs.rdi = va_arg(args, long);
    regs.rsi = va_arg(args, long);
    regs.rdx = va_arg(args, long);
    regs.r10 = va_arg(args, long);
    regs.r8 = va_arg(args, long);
    regs.r9 = va_arg(args, long);

    // Jump to a syscall instruction. Alternatively we could overwrite
    // the next instruction with a syscall instruction, but this avoids
    // weirdness associated with mutating code.
    utility_assert(thread->syscall_rip);
    regs.rip = thread->syscall_rip;

    debug("threadptrace_nativeSyscall setting regs: rip=0x%llx n=%lld arg0=0x%llx arg1=0x%llx "
          "arg2=%llx "
          "arg3=0x%llx arg4=0x%llx arg5=0x%llx",
          regs.rip, regs.rax, regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
    if (ptrace(PTRACE_SETREGS, thread->base.nativeTid, 0, &regs) < 0) {
        error("ptrace: %s", g_strerror(errno));
        abort();
    }
    // We're altering the child's actual register state, so we need to restore it from thread->regs later.
    thread->regs.dirty = true;

    // Single-step until the syscall instruction is executed. It's not clear whether we can depend
    // on stopping the exact same number of times here.
    do {
        if (ptrace(PTRACE_SINGLESTEP, thread->base.nativeTid, 0, 0) < 0) {
            error("ptrace %d: %s", thread->base.nativeTid, g_strerror(errno));
            abort();
        }
        int wstatus;
        if (_waitpid_spin(thread->base.nativeTid, &wstatus, 0) < 0) {
            error("waitpid: %s", g_strerror(errno));
            abort();
        }
        StopReason reason = _getStopReason(wstatus);
        if (reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGSTOP) {
            debug("Ignoring SIGSTOP");
            continue;
        }
        if (!(reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGTRAP)) {
            // In particular this could be an exec stop if the syscall was execve,
            // or an exited stop if the syscall was exit.
            debug("Executing native syscall changed child state");
            _threadptrace_updateChildState(thread, reason);
        }
        if (!threadptrace_isRunning(&thread->base)) {
            // Since the child is no longer running, we have no way of retrieving a
            // return value, if any. e.g. this happens after the `exit` syscall.
            return -ECHILD;
        }
        if (ptrace(PTRACE_GETREGS, thread->base.nativeTid, 0, &regs) < 0) {
            error("ptrace: %s", g_strerror(errno));
            abort();
        }
        debug("threadptrace_nativeSyscall regs: rip=%llx rax=%llx arg0=%llx arg1=%llx "
              "arg2=%llx arg3=%llx arg4=%llx arg5=%llx",
              regs.rip, regs.rax, regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
    } while (regs.rip == thread->syscall_rip);

    debug("Native syscall result %lld (%s)", regs.rax, strerror(-regs.rax));

    return regs.rax;
}

int threadptrace_clone(Thread* base, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                       PluginPtr ctid, unsigned long newtls, Thread** childp) {
    ThreadPtrace* thread = _threadToThreadPtrace(base);
    flags |= CLONE_PTRACE;
    flags &= ~(CLONE_UNTRACED);
    pid_t childNativeTid =
        thread_nativeSyscall(base, SYS_clone, flags, child_stack, ptid, ctid, newtls);
    if (childNativeTid < 0) {
        debug("native clone failed %d(%s)", childNativeTid, strerror(-childNativeTid));
        return childNativeTid;
    }
    debug("native clone created tid %d", childNativeTid);

    // The return value of the clone syscall in the child thread isn't
    // documented in clone(2), but based on the libc wrapper [is
    // zero](https://github.com/bminor/glibc/blob/5f72f9800b250410cad3abfeeb09469ef12b2438/sysdeps/unix/sysv/linux/x86_64/clone.S#L80).
    // We don't have to worry about setting it there - the OS will have already
    // done so.

    *childp =
        thread->enableIpc
            ? threadptrace_new(base->host, base->process, host_getNewProcessID(base->host))
            : threadptracenoipc_new(base->host, base->process, host_getNewProcessID(base->host));

    ThreadPtrace* child = _threadToThreadPtrace(*childp);
    child->base.nativePid = base->nativePid;
    child->base.nativeTid = childNativeTid;

    debug("cloned a new virtual thread at tid %d", child->base.tid);

    // The child should get a SIGSTOP triggered by the CLONE_PTRACE flag. Wait
    // for that stop, which puts the child into the
    // THREAD_PTRACE_CHILD_STATE_TRACE_ME state.
    int wstatus;
    if (_waitpid_spin(childNativeTid, &wstatus, 0) < 0) {
        error("waitpid: %s", g_strerror(errno));
        abort();
    }
    StopReason reason = _getStopReason(wstatus);
    utility_assert(reason.type == STOPREASON_SIGNAL && reason.signal.signal == SIGSTOP);
    child->childState = THREAD_PTRACE_CHILD_STATE_TRACE_ME;
    _threadptrace_enterStateTraceMe(child);

    if (thread->enableIpc) {
        // Send 'start' event.
        ShimEvent startEvent = {
            .event_id = SHD_SHIM_EVENT_START,
            .event_data.start = {
                .simulation_nanos = worker_getEmulatedTime(),
                .shim_shared_mem = shmemallocator_globalBlockSerialize(&child->shimSharedMemBlock),
            }};
        shimevent_sendEventToPlugin(child->ipcBlk.p, &startEvent);
    }

    return childNativeTid;
}

Thread* threadptrace_new(Host* host, Process* process, int threadID) {
    ThreadPtrace* thread = (ThreadPtrace*)threadptracenoipc_new(host, process, threadID);

    thread->ipcBlk = shmemallocator_globalAlloc(ipcData_nbytes());
    ipcData_init(thread->ipcBlk.p);
    
    thread->shimSharedMemBlock = shmemallocator_globalAlloc(sizeof(ShimSharedMem));
    thread->shimSharedMem = thread->shimSharedMemBlock.p;

    *thread->shimSharedMem = (ShimSharedMem){.ptrace_allow_native_syscalls = false};
    thread->enableIpc = true;

    return _threadPtraceToThread(thread);
}

Thread* threadptracenoipc_new(Host* host, Process* process, int threadID) {
    ThreadPtrace* thread = g_new(ThreadPtrace, 1);

    *thread = (ThreadPtrace){
        .base = thread_create(host, process, threadID, THREADPTRACE_TYPE_ID,
                              (ThreadMethods){
                                  .run = threadptrace_run,
                                  .resume = threadptrace_resume,
                                  .terminate = threadptrace_terminate,
                                  .getReturnCode = threadptrace_getReturnCode,
                                  .isRunning = threadptrace_isRunning,
                                  .free = threadptrace_free,
                                  .getReadablePtr = threadptrace_getReadablePtr,
                                  .getReadableString = threadptrace_getReadableString,
                                  .getWriteablePtr = threadptrace_getWriteablePtr,
                                  .getMutablePtr = threadptrace_getMutablePtr,
                                  .flushPtrs = threadptrace_flushPtrs,
                                  .nativeSyscall = threadptrace_nativeSyscall,
                                  .clone = threadptrace_clone,
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



