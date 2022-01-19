#include "lib/shim/shim.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <search.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shim/ipc.h"
#include "lib/shim/shadow_sem.h"
#include "lib/shim/shadow_spinlock.h"
#include "lib/shim/shim_event.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_rdtsc.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_syscall.h"
#include "lib/shim/shim_tls.h"
#include "main/host/syscall_numbers.h" // for SYS_shadow_* defs

// Definition is sometimes missing in the userspace headers. We could include
// the kernel signal header, but it has definitions that conflict with the
// userspace headers.
#ifndef SS_AUTODISARM
#define SS_AUTODISARM (1U << 31)
#endif

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
static ShMemBlock* _shim_thread_shared_mem_blk() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(ShMemBlock));
}
static ShimThreadSharedMem* _shim_thread_shared_mem() {
    if (_shim_thread_shared_mem_blk() == NULL) {
        return NULL;
    }
    return _shim_thread_shared_mem_blk()->p;
}

// Per-thread state shared with Shadow.
static ShMemBlock* _shim_process_shared_mem_blk() {
    static ShMemBlock blk;
    return &blk;
}
static ShimProcessSharedMem* _shim_process_shared_mem() {
    if (_shim_process_shared_mem_blk() == NULL) {
        return NULL;
    }
    return _shim_process_shared_mem_blk()->p;
}

// We disable syscall interposition when this is > 0.
static int* _shim_allowNativeSyscallsFlag() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(bool));
}

static void _shim_ptrace_set_allow_native_syscalls(bool is_allowed) {
    if (_shim_thread_shared_mem()) {
        _shim_thread_shared_mem()->ptrace_allow_native_syscalls = is_allowed;
        trace("%s native-syscalls via shmem %p", is_allowed ? "allowing" : "disallowing",
              _shim_thread_shared_mem);
    } else {
        // Ptrace will intercept the native syscall and handle this from within Shadow.
        shim_native_syscall(SYS_shadow_set_ptrace_allow_native_syscalls, is_allowed);
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
        if (shim_native_syscall(SYS_sched_yield)) {
            panic("shim_native_syscall(SYS_sched_yield): %s", strerror(errno));
        }
    }

    // Release the global clone lock.
    if (shadow_spin_unlock(&_startThreadLock)) {
        panic("shadow_spin_unlock: %s", strerror(errno));
    }
}

bool shim_swapAllowNativeSyscalls(bool new) {
    bool old = *_shim_allowNativeSyscallsFlag();
    *_shim_allowNativeSyscallsFlag() = new;
    if (_using_interpose_ptrace && (new != old)) {
        _shim_ptrace_set_allow_native_syscalls(new);
    }
    return old;
}

bool shim_interpositionEnabled() {
    return !*_shim_allowNativeSyscallsFlag();
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

// Any signal handlers that the shim itself installs should be configured to
// use this stack, using the `SA_ONSTACK` flag in the call to `sigaction`. This
// prevents corrupting the stack in the presence of user-space threads, such as
// goroutines.
// See https://github.com/shadow/shadow/issues/1549.
static void _shim_init_signal_stack() {
    assert(!shim_interpositionEnabled());

    // Use signed here so that we can easily detect underflow below.
    ssize_t stack_sz = SHIM_SIGNAL_STACK_SIZE;

    static ShimTlsVar new_stack_var = {0};
    void* new_stack = shimtlsvar_ptr(&new_stack_var, stack_sz);

    // Align to page boundary.
    const long page_size = sysconf(_SC_PAGESIZE);
    if ((uintptr_t)new_stack % page_size) {
        size_t padding = 0;
        padding = page_size - ((uintptr_t)new_stack % page_size);
        new_stack += padding;
        stack_sz -= padding;
    }

    // Verify that we'll still have enough space left after adjusting for padding,
    // and since we won't be able to use the guard page itself.
    if ((stack_sz - page_size) < SHIM_SIGNAL_STACK_MIN_USABLE_SIZE) {
        panic("Aligning stack to %zu page size leaves only %zd bytes (vs minimimum %zu)", page_size,
              stack_sz, SHIM_SIGNAL_STACK_MIN_USABLE_SIZE);
    }

    // Set up a guard page.
    if (mprotect(new_stack, page_size, PROT_NONE) != 0) {
        int err = errno;
        panic("mprotect: %s", strerror(err));
    }

    stack_t stack_descriptor = {
        .ss_sp = new_stack,
        .ss_size = stack_sz,
        // Clear the alternate stack settings on entry to signal handler, and
        // restore it on exit.  Otherwise a signal handler invoked while another
        // is running on the same thread would clobber the first handler's stack.
        // Instead we want the second handler to push a new frame on the alt
        // stack that's already installed.
        .ss_flags = SS_AUTODISARM,
    };

    if (sigaltstack(&stack_descriptor, NULL) != 0) {
        panic("sigaltstack: %s", strerror(errno));
    }
}

static void _shim_parent_init_logging() {
    // Set logger start time from environment variable.
    {
        const char* logger_start_time_string = getenv("SHADOW_LOG_START_TIME");
        if (!logger_start_time_string) {
            panic("Missing SHADOW_LOG_START_TIME");
        }
        int64_t logger_start_time;
        if (sscanf(logger_start_time_string, "%" PRId64, &logger_start_time) != 1) {
            panic("Couldn't parse logger start time string %s", logger_start_time_string);
        };
        logger_set_global_start_time_micros(logger_start_time);
    }

    // Redirect logger to specified log file.
    {
        const char* name = getenv("SHADOW_LOG_FILE");
        FILE* log_file = fopen(name, "w");
        if (log_file == NULL) {
            panic("fopen: %s", strerror(errno));
        }
        logger_setDefault(shimlogger_new(log_file));
    }

    // Set log level
    {
        const char* level_string = getenv("SHADOW_LOG_LEVEL");
        if (!level_string) {
            panic("Missing SHADOW_LOG_LEVEL");
        }
        int level;
        if (sscanf(level_string, "%d", &level) != 1) {
            panic("Couldn't parse log level %s", level_string);
        };
        logger_setLevel(logger_getDefault(), level);
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
    if (!shadow_pid_str) {
        panic("SHADOW_PID not set");
    }

    if (sscanf(shadow_pid_str, "%llu", &shadow_pid) != 1) {
        panic("SHADOW_PID does not contain an unsigned: %s", shadow_pid_str);
    }

    if (getppid() != shadow_pid) { // Validate that Shadow is still alive.
        error("Shadow exited.");
        exit(EXIT_FAILURE);
    }

    trace("Plugin verified Shadow is still running as parent.");
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

static void _shim_parent_init_process_shm() {
    const char* shm_blk_buf = getenv("SHADOW_SHM_PROCESS_BLK");
    assert(shm_blk_buf);

    bool err = false;
    ShMemBlockSerialized shm_blk_serialized = shmemblockserialized_fromString(shm_blk_buf, &err);

    *_shim_process_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(_shim_process_shared_mem());
}

static void _shim_parent_init_thread_shm() {
    assert(_using_interpose_ptrace);

    const char* shm_blk_buf = getenv("SHADOW_SHM_THREAD_BLK");
    assert(shm_blk_buf);

    bool err = false;
    ShMemBlockSerialized shm_blk_serialized = shmemblockserialized_fromString(shm_blk_buf, &err);

    *_shim_thread_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(_shim_thread_shared_mem());
}

static void _shim_child_init_thread_shm() {
    assert(_using_interpose_ptrace);

    assert(!_shim_thread_shared_mem());
    ShMemBlockSerialized shm_blk_serialized;

    // We execute this natively and ptrace will intercept and handle from within Shadow.
    int rv = shim_native_syscall(SYS_shadow_get_shm_blk, &shm_blk_serialized);
    if (rv != 0) {
        panic("shadow_get_shm_blk: %s", strerror(errno));
        abort();
    }

    *_shim_thread_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(_shim_thread_shared_mem());
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

static void _shim_parent_init_memory_manager_internal() {
    syscall(SYS_shadow_init_memory_manager);
}

// Tell Shadow to initialize the MemoryManager, which includes remapping the
// stack.
static void _shim_parent_init_memory_manager() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    // Temporarily allocate some memory for a separate stack. The MemoryManager
    // is going to remap the original stack, and we can't actively use it while
    // it does so.
    const size_t stack_sz = 4096*10;
    void *stack = mmap(NULL, stack_sz, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        panic("mmap: %s", strerror(errno));
    }

    ucontext_t remap_ctx, orig_ctx;
    if (getcontext(&remap_ctx) != 0) {
        panic("getcontext: %s", strerror(errno));
    }

    // Run on our temporary stack.
    remap_ctx.uc_stack.ss_sp = stack;
    remap_ctx.uc_stack.ss_size = stack_sz;

    // Return to the original ctx (which is initialized by swapcontext, below).
    remap_ctx.uc_link = &orig_ctx;

    makecontext(&remap_ctx, _shim_parent_init_memory_manager_internal, 0);

    // Call _shim_parent_init_memory_manager_internal on the configured stack.
    // Returning from _shim_parent_init_memory_manager_internal will return to
    // here.
    if (swapcontext(&orig_ctx, &remap_ctx) != 0) {
        panic("swapcontext: %s", strerror(errno));
    }

    if (munmap(stack, stack_sz) != 0) {
        panic("munmap: %s", strerror(errno));
    }

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
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
}

static void _shim_ipc_wait_for_start_event() {
    assert(_using_interpose_preload);
    assert(shim_thisThreadEventIPC());

    ShimEvent event;
    trace("waiting for start event on %p", shim_thisThreadEventIPC);
    shimevent_recvEventFromShadow(shim_thisThreadEventIPC(), &event, /* spin= */ true);
    assert(event.event_id == SHD_SHIM_EVENT_START);
}

static void _shim_parent_init_ptrace() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_parent_init_process_shm();
    _shim_parent_init_logging();
    _shim_parent_init_thread_shm();
    _shim_parent_init_memory_manager();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

static void _shim_parent_init_seccomp() {
    shim_seccomp_init();
}

static void _shim_parent_init_rdtsc_emu() {
    shim_rdtsc_init();
}

static void _shim_parent_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    // The shim logger internally disables interposition while logging, so we open the log
    // file with interposition disabled too to get a native file descriptor.
    _shim_parent_init_process_shm();
    _shim_parent_init_logging();
    _shim_parent_init_ipc();
    _shim_init_signal_stack();
    _shim_parent_init_death_signal();
    _shim_ipc_wait_for_start_event();
    _shim_parent_init_memory_manager();
    _shim_parent_init_rdtsc_emu();
    if (getenv("SHADOW_USE_SECCOMP") != NULL) {
        _shim_parent_init_seccomp();
    }

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

static void _shim_child_init_ptrace() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_child_init_thread_shm();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

static void _shim_child_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_preload_only_child_init_ipc();
    _shim_init_signal_stack();
    _shim_preload_only_child_ipc_wait_for_start_event();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

// This function should be called before any wrapped syscall. We also use the
// constructor attribute to be completely sure that it's called before main.
__attribute__((constructor)) void _shim_load() {
    static bool did_global_pre_init = false;
    if (!did_global_pre_init) {
        // Early init; must not make any syscalls.

        did_global_pre_init = true;

        // Avoid logging until we've set up the shim logger.
        logger_setLevel(logger_getDefault(), LOGLEVEL_WARNING);

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
    if (_shim_process_shared_mem() == NULL) {
        return NULL;
    } else {
        return &_shim_process_shared_mem()->sim_time;
    }
}
