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
#include "lib/shim/shadow_sem.h"
#include "lib/shim/shadow_spinlock.h"
#include "lib/shim/shim_event.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_syscall.h"
#include "lib/shim/shim_tls.h"
#include "lib/tsc/tsc.h"

// Whether Shadow is using preload-based interposition.
static bool _using_interpose_preload = false;

// Whether Shadow is using ptrace-based interposition.
static bool _using_interpose_ptrace = false;

// Whether Shadow is using the shim-side syscall handler optimization.
static bool _using_shim_syscall_handler = true;

// This thread's IPC block, for communication with Shadow.
static ShMemBlock* _shim_ipcDataBlk() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(ShMemBlock));
}
struct IPCData* shim_thisThreadEventIPC() {
    return _shim_ipcDataBlk()->p;
}

// Per-thread state shared with Shadow.
static ShMemBlock* _shim_shared_mem_blk() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(ShMemBlock));
}
static ShimSharedMem* _shim_shared_mem() {
    return _shim_shared_mem_blk()->p;
}

// We disable syscall interposition when this is > 0.
static int* _shim_disable_interposition() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(int));
}

static void _shim_set_allow_native_syscalls(bool is_allowed) {
    if (_shim_shared_mem()) {
        _shim_shared_mem()->ptrace_allow_native_syscalls = is_allowed;
        trace("%s native-syscalls via shmem %p", is_allowed ? "allowing" : "disallowing",
              _shim_shared_mem);
    } else {
        shadow_set_ptrace_allow_native_syscalls(is_allowed);
        trace("%s native-syscalls via custom syscall", is_allowed ? "allowing" : "disallowing");
    }
}

// Held from the time of starting to initialize _startThread, to being done with
// it. i.e. ensure we don't try to start more than one thread at once.
//
// For example, this prevents the child thread, after having initialized itself
// and released the parent via the childInitd semaphore, from starting another
// clone itself until after the parent has woken up and released this lock.
static shadow_spinlock_t _startThreadLock = SHADOW_SPINLOCK_STATICALLY_INITD;
static struct {
    ShMemBlock childIpcBlk;
    shadow_sem_t childInitd;
} _startThread;

void shim_newThreadStart(ShMemBlockSerialized* block) {
    if (shadow_spin_lock(&_startThreadLock)) {
        panic("shadow_spin_lock: %s", strerror(errno));
    };
    if (shadow_sem_init(&_startThread.childInitd, 0, 0)) {
        panic("shadow_sem_init: %s", strerror(errno));
    }
    _startThread.childIpcBlk = shmemserializer_globalBlockDeserialize(block);
}

void shim_newThreadChildInitd() {
    if (shadow_sem_post(&_startThread.childInitd)) {
        panic("shadow_sem_post: %s", strerror(errno));
    }
}

void shim_newThreadFinish() {
    // Wait for child to initialize itself.
    while (shadow_sem_trywait(&_startThread.childInitd)) {
        if (errno != EAGAIN) {
            panic("shadow_sem_trywait: %s", strerror(errno));
        }
        if (shadow_real_raw_syscall(SYS_sched_yield)) {
            panic("shadow_real_raw_syscall(SYS_sched_yield): %s", strerror(errno));
        }
    }

    // Release the global clone lock.
    if (shadow_spin_unlock(&_startThreadLock)) {
        panic("shadow_spin_unlock: %s", strerror(errno));
    }
}

bool shim_disableInterposition() {
    if (++*_shim_disable_interposition() == 1) {
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
    if (--*_shim_disable_interposition() == 0) {
        if (_using_interpose_ptrace && _using_interpose_preload) {
            _shim_set_allow_native_syscalls(false);
        }
        return true;
    } else {
        return false;
    }
}

bool shim_interpositionEnabled() {
    return _using_interpose_preload && !*_shim_disable_interposition();
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

    *_shim_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(_shim_shared_mem());
}

static void _shim_child_init_shm() {
    assert(_using_interpose_ptrace);

    assert(!_shim_shared_mem());
    ShMemBlockSerialized shm_blk_serialized;
    int rv = shadow_get_shm_blk(&shm_blk_serialized);
    if (rv != 0) {
        panic("shadow_get_shm_blk: %s", strerror(errno));
        abort();
    }

    *_shim_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(_shim_shared_mem());
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

static void _shim_preload_only_child_init_ipc() {
    assert(_using_interpose_preload);
    assert(!_using_interpose_ptrace);

    *_shim_ipcDataBlk() = _startThread.childIpcBlk;
}

static void _shim_preload_only_child_ipc_wait_for_start_event() {
    assert(_using_interpose_preload);
    assert(shim_thisThreadEventIPC());

    ShimEvent event;
    trace("waiting for start event on %p", shim_thisThreadEventIPC);

    // We're returning control to the parent thread here, who is going to switch
    // back to their own TLS.
    struct IPCData* ipc = shim_thisThreadEventIPC();

    // Releases parent thread, who switches back to their own TLS.  i.e. Don't
    // use TLS between here and when we can switch back to our own after
    // receiving the start event.
    shim_newThreadChildInitd();

    shimevent_recvEventFromShadow(ipc, &event, /* spin= */ true);
    assert(event.event_id == SHD_SHIM_EVENT_START);
    shim_syscall_set_simtime_nanos(event.event_data.start.simulation_nanos);
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

// When emulating a clone syscall, we need to jump to just after the original
// syscall instruction in the child thread. This stores that address.
static ShimTlsVar _shim_clone_rip_var = {0};
static void** _shim_clone_rip() {
    return shimtlsvar_ptr(&_shim_clone_rip_var, sizeof(void*));
}

void* shim_take_clone_rip() {
    void *ptr = *_shim_clone_rip();
    *_shim_clone_rip() = NULL;
    return ptr;
}

static void _handle_sigsys(int sig, siginfo_t* info, void* voidUcontext) {
    ucontext_t* ctx = (ucontext_t*)(voidUcontext);
    if (sig != SIGSYS) {
        abort();
    }
    greg_t* regs = ctx->uc_mcontext.gregs;
    const int REG_N =  REG_RAX;
    const int REG_ARG1 = REG_RDI;
    const int REG_ARG2 = REG_RSI;
    const int REG_ARG3 = REG_RDX;
    const int REG_ARG4 = REG_R10;
    const int REG_ARG5 = REG_R8;
    const int REG_ARG6 = REG_R9;

    trace("Trapped syscall %lld", regs[REG_N]);

    if (regs[REG_N] == SYS_clone) {
       assert(!*_shim_clone_rip());
       *_shim_clone_rip() = (void*)regs[REG_RIP];
    }

    // Make the syscall via the *the shim's* syscall function (which overrides
    // libc's).  It in turn will either emulate it or (if interposition is
    // disabled), make the call natively. In the latter case, the syscall
    // will be permitted to execute by the seccomp filter.
    long rv = shadow_raw_syscall(regs[REG_N], regs[REG_ARG1], regs[REG_ARG2], regs[REG_ARG3], regs[REG_ARG4], regs[REG_ARG5], regs[REG_ARG6]);
    trace("Trapped syscall %lld returning %ld", ctx->uc_mcontext.gregs[REG_RAX], rv);
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
        BPF_JUMP(BPF_JMP + BPF_JGT + BPF_K, ((long)shadow_vreal_raw_syscall) + 2000,
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

static void _handle_sigsegv(int sig, siginfo_t* info, void* voidUcontext) {
    trace("Trapped sigsegv");
    static bool tsc_initd = false;
    static Tsc tsc;
    if (!tsc_initd) {
        trace("Initializing tsc");
        tsc = Tsc_init();
        tsc_initd = true;
    }

    ucontext_t* ctx = (ucontext_t*)(voidUcontext);
    greg_t* regs = ctx->uc_mcontext.gregs;

    unsigned char* insn = (unsigned char*)regs[REG_RIP];
    if (isRdtsc(insn)) {
        trace("Emulating rdtsc");
        uint64_t rax, rdx;
        uint64_t rip = regs[REG_RIP];
        Tsc_emulateRdtsc(&tsc, &rax, &rdx, &rip, shim_syscall_get_simtime_nanos());
        regs[REG_RDX] = rdx;
        regs[REG_RAX] = rax;
        regs[REG_RIP] = rip;
        return;
    }
    if (isRdtscp(insn)) {
        trace("Emulating rdtscp");
        uint64_t rax, rdx, rcx;
        uint64_t rip = regs[REG_RIP];
        Tsc_emulateRdtscp(&tsc, &rax, &rdx, &rcx, &rip, shim_syscall_get_simtime_nanos());
        regs[REG_RDX] = rdx;
        regs[REG_RAX] = rax;
        regs[REG_RCX] = rcx;
        regs[REG_RIP] = rip;
        return;
    }
    error("Unhandled sigsegv");

    // We don't have the "normal" segv signal handler to fall back on, but the
    // sigabrt handler typically does the same thing - dump core and exit with a
    // failure.
    raise(SIGABRT);
}

static void _shim_parent_init_rdtsc_emu() {
    // Force a SEGV on any rdtsc or rdtscp instruction.
    if (prctl(PR_SET_TSC, PR_TSC_SIGSEGV) < 0) {
        panic("pctl: %s", strerror(errno));
    }

    // Install our own handler to emulate.
    if (sigaction(SIGSEGV,
                  &(struct sigaction){
                      .sa_sigaction = _handle_sigsegv,
                      // SA_NODEFER: Allow recursive signal handling, to handle a syscall
                      // being made during the handling of another. For example, we need this
                      // to properly handle the case that we end up logging from the syscall
                      // handler, and the IO syscalls themselves are trapped.
                      // SA_SIGINFO: Required because we're specifying sa_sigaction.
                      .sa_flags = SA_NODEFER | SA_SIGINFO,
                  },
                  NULL) < 0) {
        panic("sigaction: %s", strerror(errno));
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
    _shim_parent_init_rdtsc_emu();
    if (getenv("SHADOW_USE_SECCOMP") != NULL) {
        _shim_parent_init_seccomp();
    }

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

    _shim_preload_only_child_init_ipc();
    _shim_preload_only_child_ipc_wait_for_start_event();

    shim_enableInterposition();
}

// This function should be called before any wrapped syscall. We also use the
// constructor attribute to be completely sure that it's called before main.
__attribute__((constructor)) void _shim_load() {
    static bool did_global_pre_init = false;
    if (!did_global_pre_init) {
        // Early init; must not make any syscalls.
        did_global_pre_init = true;
        _set_interpose_type();
        _set_use_shim_syscall_handler();
    }

    // Now we can use thread-local storage.
    static ShimTlsVar started_thread_init_var = {0};
    bool* started_thread_init =
        shimtlsvar_ptr(&started_thread_init_var, sizeof(*started_thread_init));
    if (*started_thread_init) {
        // Avoid deadlock when _shim_global_init's syscalls caused this function to be
        // called recursively.  In the uninitialized state,
        // `shim_interpositionEnabled` returns false, allowing _shim_global_init's
        // syscalls to execute natively.
        return;
    }
    *started_thread_init = true;

    static bool did_global_init = false;
    if (!did_global_init) {
        if (_using_interpose_ptrace) {
            _shim_parent_init_ptrace();
        } else if (_using_interpose_preload) {
            _shim_parent_init_preload();
        }
        did_global_init = true;
        trace("Finished shim parent init");
    } else {
        if (_using_interpose_ptrace) {
            _shim_child_init_ptrace();
        } else if (_using_interpose_preload) {
            _shim_child_init_preload();
        }
        trace("Finished shim child init");
    }
}

void shim_ensure_init() { _shim_load(); }

struct timespec* shim_get_shared_time_location() {
    if (_shim_shared_mem() == NULL) {
        return NULL;
    } else {
        return &_shim_shared_mem()->sim_time;
    }
}
