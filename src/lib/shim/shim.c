#include "lib/shim/shim.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <search.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shim/ipc.h"
#include "lib/shim/preload_syscall.h"
#include "lib/shim/shim_event.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_syscall.h"

// Whether Shadow is using preload-based interposition.
static bool _using_interpose_preload = false;

// Whether Shadow is using ptrace-based interposition.
static bool _using_interpose_ptrace = false;

// Whether Shadow is using the shim-side syscall handler optimization.
static bool _using_shim_syscall_handler = true;

typedef struct ShimThreadLocalStorage {
    alignas(16) char _bytes[1024];
} ShimThreadLocalStorage;
static ShimThreadLocalStorage _shim_tlss[100];
static size_t _shim_tls_byte_offset = 0;

// First thread, where global init will be performed, always gets index 0.
static int _shim_current_tls_idx = 0;
static int _shim_getCurrentTlsIdx() {
    assert(_using_interpose_preload || _using_interpose_ptrace);
    if (_using_interpose_ptrace) {
        // Thread locals supported.
        static int next_idx = 0;
        static __thread int idx = -1;
        if (idx == -1) {
            idx = next_idx++;
        }
        return idx;
    }
    // Unsafe to use thread-locals. idx will be set explicitly through shim IPC.
    return _shim_current_tls_idx;
}

typedef struct ShimThreadLocalVar {
    size_t offset;
    bool initd;
} ShimThreadLocalVar;

// Initialize storage and return whether it had already been initialized.
void* stlv_ptr(ShimThreadLocalVar* v, size_t sz) {
    int idx = _shim_getCurrentTlsIdx();
    if (!v->initd) {
        v->offset = _shim_tls_byte_offset;
        _shim_tls_byte_offset += sz;

        // Always leave aligned at 16 for simplicity.
        // 16 is a safe alignment for any C primitive.
        size_t overhang = _shim_tls_byte_offset % 16;
        _shim_tls_byte_offset += (16 - overhang);

        assert(_shim_tls_byte_offset  < sizeof(ShimThreadLocalStorage));
        v->initd = true;
    }
    return &_shim_tlss[idx]._bytes[v->offset];
}

// This thread's IPC block, for communication with Shadow.
static ShMemBlock* _shim_ipcDataBlk() {
    static ShimThreadLocalVar v = {0};
    return stlv_ptr(&v, sizeof(ShMemBlock));
}
struct IPCData* shim_thisThreadEventIPC() {
    return _shim_ipcDataBlk()->p;
}

// Per-thread state shared with Shadow.
static __thread ShMemBlock _shim_shared_mem_blk = {0};
static __thread ShimSharedMem* _shim_shared_mem = NULL;

// We disable syscall interposition when this is > 0.
static __thread int _shim_disable_interposition = 0;

static void _shim_set_allow_native_syscalls(bool is_allowed) {
    if (_shim_shared_mem) {
        _shim_shared_mem->ptrace_allow_native_syscalls = is_allowed;
        trace("%s native-syscalls via shmem %p", is_allowed ? "allowing" : "disallowing",
              _shim_shared_mem);
    } else {
        shadow_set_ptrace_allow_native_syscalls(is_allowed);
        trace("%s native-syscalls via custom syscall", is_allowed ? "allowing" : "disallowing");
    }
}

bool shim_disableInterposition() {
    if (++_shim_disable_interposition == 1) {
        if (_using_interpose_ptrace && _using_interpose_preload) {
            _shim_set_allow_native_syscalls(true);
        }
        return true;
    } else {
        return false;
    }
}

bool shim_enableInterposition() {
    assert(_shim_disable_interposition > 0);
    if (--_shim_disable_interposition == 0) {
        if (_using_interpose_ptrace && _using_interpose_preload) {
            _shim_set_allow_native_syscalls(false);
        }
        return true;
    } else {
        return false;
    }
}

bool shim_interpositionEnabled() {
    return _using_interpose_preload && !_shim_disable_interposition;
}

bool shim_use_syscall_handler() { return _using_shim_syscall_handler; }

// Figure out what interposition mechanism we're using, based on environment
// variables.  This is called before disabling interposition, so should be
// careful not to make syscalls.
static void _set_interpose_type() {
    // If we're not running under Shadow, return. This can be useful
    // for testing the libc parts of the shim.
    if (!getenv("SHADOW_SPAWNED")) {
        return;
    }

    const char* interpose_method = getenv("SHADOW_INTERPOSE_METHOD");
    assert(interpose_method);
    if (!strcmp(interpose_method, "PRELOAD")) {
        // Uses library preloading to intercept syscalls.
        _using_interpose_preload = true;
        return;
    }
    if (!strcmp(interpose_method, "HYBRID")) {
        // Uses library preloading to intercept syscalls, with a ptrace backstop.
        _using_interpose_preload = true;
        _using_interpose_ptrace = true;
        return;
    }
    if (!strcmp(interpose_method, "PTRACE")) {
        // From the shim's point of view, behave as if it's not running under
        // Shadow, and let all control happen via ptrace.
        _using_interpose_ptrace = true;
        return;
    }
    abort();
}

static void _set_use_shim_syscall_handler() {
    const char* shim_syscall_str = getenv("SHADOW_DISABLE_SHIM_SYSCALL");
    if (shim_syscall_str && !strcmp(shim_syscall_str, "TRUE")) {
        _using_shim_syscall_handler = false;
    } else {
        _using_shim_syscall_handler = true;
    }
}

static void _shim_parent_init_logging() {
    // Set logger start time from environment variable.
    {
        const char* logger_start_time_string = getenv("SHADOW_LOG_START_TIME");
        assert(logger_start_time_string);
        int64_t logger_start_time;
        assert(sscanf(logger_start_time_string, "%" PRId64, &logger_start_time) == 1);
        logger_set_global_start_time_micros(logger_start_time);
    }

    // Redirect logger to specified log file.
    {
        const char* name = getenv("SHADOW_LOG_FILE");
        FILE* log_file = fopen(name, "w");
        if (log_file == NULL) {
            perror("fopen");
            abort();
        }
        logger_setDefault(shimlogger_new(log_file));
    }
}

/*
 * If we can parse it from the env, check that Shadow's PID is my parent and
 * exit otherwise.
 */
static void _verify_parent_pid_or_exit() {
    unsigned long long shadow_pid = 0;
    bool valid_parse_pid = false;
    const char* shadow_pid_str = getenv("SHADOW_PID");

    if (shadow_pid_str) {
        int rc = sscanf(shadow_pid_str, "%llu", &shadow_pid);

        if (rc == 1) {
            valid_parse_pid = true;
        } else {
            panic("SHADOW_PID does not contain an unsigned: %s", shadow_pid_str);
        }
    }

    if (valid_parse_pid) {
        if (getppid() == shadow_pid) { // Validate that Shadow is still alive.
            trace("Plugin verified Shadow is still running as parent.");
        } else {
            panic("Shadow exited.");
            exit(-1); // If Shadow's dead, we can just get out(?)
        }
    }
}

static void _shim_parent_init_death_signal() {
    // Ensure that the child process exits when Shadow does.  Shadow
    // ought to have already tried to terminate the child via SIGTERM
    // before shutting down (though see
    // https://github.com/shadow/shadow/issues/903), so now we jump all
    // the way to SIGKILL.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
        warning("prctl: %s", strerror(errno));
    }

    _verify_parent_pid_or_exit();
}

static void _shim_parent_init_shm() {
    assert(_using_interpose_ptrace);

    const char* shm_blk_buf = getenv("SHADOW_SHM_BLK");
    assert(shm_blk_buf);

    bool err = false;
    ShMemBlockSerialized shm_blk_serialized = shmemblockserialized_fromString(shm_blk_buf, &err);

    _shim_shared_mem_blk = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    _shim_shared_mem = _shim_shared_mem_blk.p;

    if (!_shim_shared_mem) {
        abort();
    }
}

static void _shim_child_init_shm() {
    assert(_using_interpose_ptrace);

    // If we haven't initialized the shm block yet (because this isn't the main thread,
    // which is initialized in the global initialization via an environment variable), do so.
    if (!_shim_shared_mem) {
        ShMemBlockSerialized shm_blk_serialized;
        int rv = shadow_get_shm_blk(&shm_blk_serialized);
        if (rv != 0) {
            panic("shadow_get_shm_blk: %s", strerror(errno));
            abort();
        }

        _shim_shared_mem_blk = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
        _shim_shared_mem = _shim_shared_mem_blk.p;
        if (!_shim_shared_mem) {
            abort();
        }
    }
}

static void _shim_parent_init_ipc() {
    assert(_using_interpose_preload);

    const char* ipc_blk_buf = getenv("SHADOW_IPC_BLK");
    assert(ipc_blk_buf);
    bool err = false;
    ShMemBlockSerialized ipc_blk_serialized = shmemblockserialized_fromString(ipc_blk_buf, &err);
    assert(!err);

    *_shim_ipcDataBlk() = shmemserializer_globalBlockDeserialize(&ipc_blk_serialized);
    assert(shim_thisThreadEventIPC());
}

static void _shim_child_init_ipc() {
    assert(_using_interpose_preload);
    assert(_using_interpose_ptrace);

    assert(!shim_thisThreadEventIPC());
    ShMemBlockSerialized ipc_blk_serialized;
    int rv = shadow_get_ipc_blk(&ipc_blk_serialized);
    if (rv != 0) {
        panic("shadow_get_ipc_blk: %s", strerror(errno));
        abort();
    }
    *_shim_ipcDataBlk() = shmemserializer_globalBlockDeserialize(&ipc_blk_serialized);
    assert(shim_thisThreadEventIPC());
}

static void _shim_ipc_wait_for_start_event() {
    assert(_using_interpose_preload);
    assert(shim_thisThreadEventIPC());

    ShimEvent event;
    trace("waiting for start event on %p", shim_thisThreadEventIPC);
    shimevent_recvEventFromShadow(shim_thisThreadEventIPC(), &event, /* spin= */ true);
    assert(event.event_id == SHD_SHIM_EVENT_START);
    shim_syscall_set_simtime_nanos(event.event_data.start.simulation_nanos);
}

static void _shim_parent_init_hybrid() {
    shim_disableInterposition();

    // The shim logger internally disables interposition while logging, so we open the log
    // file with interposition disabled too to get a native file descriptor.
    _shim_parent_init_logging();
    _shim_parent_init_shm();
    _shim_parent_init_ipc();
    _shim_parent_init_death_signal();
    _shim_ipc_wait_for_start_event();

    shim_enableInterposition();
}

static void _shim_parent_init_ptrace() {
    // In ptrace mode, shim_disableInterposition *doesn't* actually prevent ptrace from
    // interposing. This means that the logger operations will later be interposed, so
    // we want this open operation to be interposed too so we get a shadow file descriptor
    // this will be valid on later logging operations.
    _shim_parent_init_logging();

    // Disable interposition does not prevent ptrace interposition. We need to override
    // that here to correctly load the shm block.
    if (shim_disableInterposition()) {
        _shim_set_allow_native_syscalls(true);
    }

    _shim_parent_init_shm();
    _shim_parent_init_death_signal();

    if (shim_enableInterposition()) {
        _shim_set_allow_native_syscalls(false);
    }
}

static void _handle_sigsys(int sig, siginfo_t* info, void* voidUcontext) {
    ucontext_t* ctx = (ucontext_t*)(voidUcontext);
    if (sig != SIGSYS) {
        abort();
    }
    // Make the syscall via the *the shim's* syscall function (which overrides
    // libc's).  It in turn will either emulate it or (if interposition is
    // disabled), or make the call natively. In the latter case, the syscall
    // will be permitted to execute by the seccomp filter.
    long rv = shadow_raw_syscall(ctx->uc_mcontext.gregs[REG_RAX], ctx->uc_mcontext.gregs[REG_RDI],
                                 ctx->uc_mcontext.gregs[REG_RSI], ctx->uc_mcontext.gregs[REG_RDX],
                                 ctx->uc_mcontext.gregs[REG_R10], ctx->uc_mcontext.gregs[REG_R8],
                                 ctx->uc_mcontext.gregs[REG_R9]);
    ctx->uc_mcontext.gregs[REG_RAX] = rv;
}

static void _shim_parent_init_seccomp() {
    // Install signal sigsys signal handler, which will receive syscalls that
    // get stopped by the seccomp filter. Shadow's emulation of signal-related
    // system calls will prevent this action from later being overridden by the
    // virtual process.
    struct sigaction old_action;
    if (sigaction(SIGSYS,
                  &(struct sigaction){
                      .sa_sigaction = _handle_sigsys,
                      // SA_NODEFER: Allow recursive signal handling, to handle a syscall
                      // being made during the handling of another. For example, we need this
                      // to properly handle the case that we end up logging from the syscall
                      // handler, and the IO syscalls themselves are trapped.
                      // SA_SIGINFO: Required because we're specifying sa_sigaction.
                      .sa_flags = SA_NODEFER | SA_SIGINFO,
                  },
                  &old_action) < 0) {
        panic("sigaction: %s", strerror(errno));
    }
    if (old_action.sa_handler || old_action.sa_sigaction) {
        warning("Overwrite handler for SIGSYS (%p)", old_action.sa_handler
                                                         ? (void*)old_action.sa_handler
                                                         : (void*)old_action.sa_sigaction);
    }

    // Ensure that SIGSYS isn't blocked. This code runs in the process's first
    // thread, so the resulting mask will be inherited by subsequent threads.
    // Shadow's emulation of signal-related system calls will prevent it from
    // later becoming blocked.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGSYS);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
        panic("sigprocmask: %s", strerror(errno));
    }

    // Setting PR_SET_NO_NEW_PRIVS allows us to install a seccomp filter without
    // CAP_SYS_ADMIN.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        panic("prctl: %s", strerror(errno));
    }

    /* A bpf program to be loaded as a `seccomp` filter. Unfortunately the
     * documentation for how to write this is pretty sparse. There's a useful
     * example in samples/seccomp/bpf-direct.c of the Linux kernel source tree.
     * The best reference I've been able to find is a BSD man page:
     * https://www.freebsd.org/cgi/man.cgi?query=bpf&sektion=4&manpath=FreeBSD+4.7-RELEASE
     */
    struct sock_filter filter[] = {
        /* accumulator := syscall number */
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, nr)),

        /* Always allow sigreturn; otherwise we'd crash returning from our signal handler. */
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_rt_sigreturn, /*true-skip=*/0, /*false-skip=*/1),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

        /* Always allow sched_yield. Sometimes used in IPC with Shadow; emulating
         * would just add unnecessary overhead.  */
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_sched_yield, /*true-skip=*/0, /*false-skip=*/1),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

        /* FIXME: TEMP Always allow futex. This will will end up causing deadlock
         * if there's actually contention on a futex.  i.e. we'll need to get rid
         * of this to be compatible with threaded virtual processes. */
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_futex, /*true-skip=*/0, /*false-skip=*/1),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

        /* See if instruction pointer is within the shadow_vreal_raw_syscall fn. */
        /* accumulator := instruction_pointer */
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, instruction_pointer)),
        /* If it's in `shadow_vreal_raw_syscall`, allow. We don't know the end address, but it
         * should be safe-ish to check if it's within a kilobyte or so. We know there are no
         * other syscall instructions within this library, so the only problem would be if
         * shadow_vreal_raw_syscall ended up at the very end of the library object, and a syscall
         * ended up being made from the very beginning of another library object, loaded just
         * after ours.
         *
         * TODO: Consider using the actual bounds of this object file, from /proc/self/maps. */
        BPF_JUMP(BPF_JMP + BPF_JGT + BPF_K, ((long)shadow_vreal_raw_syscall) + 1000,
                 /*true-skip=*/2, /*false-skip=*/0),
        BPF_JUMP(BPF_JMP + BPF_JGE + BPF_K, (long)shadow_vreal_raw_syscall, /*true-skip=*/0,
                 /*false-skip=*/1),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

    /* This block was intended to whitelist reads and writes to a socket
     * used to communicate with Shadow. It turns out to be unnecessary though,
     * because the functions we're using are already wrapped, and so go through
     * shadow_vreal_raw_syscall, and so end up already being whitelisted above based on that.
     * (Also ended up switching back to shared-mem-based IPC instead of a socket).
     *
     * Keeping the code around for now in case we end up needing it or something similar.
     */
#if 0
        /* check_socket: Allow reads and writes to shadow socket */
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, SYS_read, 0, 2/*check_fd*/),
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, SYS_write, 0, 1/*check_fd*/),
        /* Skip to instruction pointer check */
        BPF_JUMP(BPF_JMP+BPF_JA, 3/* check_ip */, 0, 0),
        /* check_fd */
        /* accumulator := arg1 */
        BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, toShadowFd, 0, 1/* check_ip */),
        BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
#endif

        /* Trap to our syscall handler */
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP),
    };
    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    // Re SECCOMP_FILTER_FLAG_SPEC_ALLOW: Without this flag, installing a
    // seccomp filter sets the PR_SPEC_FORCE_DISABLE bit (see prctl(2)). This
    // results in a significant performance penalty. Meanwhile Shadow is
    // semi-cooperative with its virtual processes; it doesn't try to protect
    // itself or the system from malicious code. Hence, it isn't worth paying
    // this overhead.
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_SPEC_ALLOW, &prog)) {
        panic("seccomp: %s", strerror(errno));
    }
}

static void _shim_parent_init_preload() {
    shim_disableInterposition();

    // The shim logger internally disables interposition while logging, so we open the log
    // file with interposition disabled too to get a native file descriptor.
    _shim_parent_init_logging();
    _shim_parent_init_ipc();
    _shim_parent_init_death_signal();
    _shim_ipc_wait_for_start_event();
    if (getenv("SHADOW_USE_SECCOMP") != NULL) {
        _shim_parent_init_seccomp();
    }

    shim_enableInterposition();
}

static void _shim_child_init_hybrid() {
    shim_disableInterposition();

    _shim_child_init_shm();
    _shim_child_init_ipc();
    _shim_ipc_wait_for_start_event();

    shim_enableInterposition();
}

static void _shim_child_init_ptrace() {
    // Disable interposition does not prevent ptrace interposition. We need to override
    // that here to correctly load the shm block.
    if (shim_disableInterposition()) {
        _shim_set_allow_native_syscalls(true);
    }

    _shim_child_init_shm();

    if (shim_enableInterposition()) {
        _shim_set_allow_native_syscalls(false);
    }
}

static void _shim_child_init_preload() {
    shim_disableInterposition();

    _shim_child_init_ipc();
    _shim_ipc_wait_for_start_event();

    shim_enableInterposition();
}

// This function should be called before any wrapped syscall. We also use the
// constructor attribute to be completely sure that it's called before main.
__attribute__((constructor)) void _shim_load() {
    static __thread bool started_thread_init = false;
    if (started_thread_init) {
        // Avoid deadlock when _shim_global_init's syscalls caused this function to be
        // called recursively.  In the uninitialized state,
        // `shim_interpositionEnabled` returns false, allowing _shim_global_init's
        // syscalls to execute natively.
        return;
    }
    started_thread_init = true;

    // We must set the interposition type before calling
    // shim_disableInterposition.
    _set_interpose_type();
    _set_use_shim_syscall_handler();

    // Initialization tasks depend on interpose type and parent/child thread status.
    static bool did_global_init = false;
    if (!did_global_init) {
        if (_using_interpose_ptrace && _using_interpose_preload) {
            _shim_parent_init_hybrid();
        } else if (_using_interpose_ptrace) {
            _shim_parent_init_ptrace();
        } else if (_using_interpose_preload) {
            _shim_parent_init_preload();
        }
        did_global_init = true;
        trace("Finished shim parent init");
    } else {
        if (_using_interpose_ptrace && _using_interpose_preload) {
            _shim_child_init_hybrid();
        } else if (_using_interpose_ptrace) {
            _shim_child_init_ptrace();
        } else if (_using_interpose_preload) {
            _shim_child_init_preload();
        }
        trace("Finished shim child init");
    }
}

void shim_ensure_init() { _shim_load(); }

__attribute__((destructor)) static void _shim_unload() {
    if (!_using_interpose_preload) {
        // Nothing to tear down.
        return;
    }

    if (_using_interpose_ptrace) {
        // No need for explicit teardown; ptrace will detect the process exit.
        return;
    }

    shim_disableInterposition();

    struct IPCData* ipc = shim_thisThreadEventIPC();
    ShimEvent shim_event;
    shim_event.event_id = SHD_SHIM_EVENT_STOP;
    trace("sending stop event on %p", ipc);
    shimevent_sendEventToShadow(ipc, &shim_event);

    // Leave interposition disabled; shadow is waiting for
    // this process to die and won't listen to the shim pipe anymore.
}

struct timespec* shim_get_shared_time_location() {
    if (_shim_shared_mem == NULL) {
        return NULL;
    } else {
        return &_shim_shared_mem->sim_time;
    }
}
