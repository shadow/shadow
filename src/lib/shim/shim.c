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
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shadow_sem.h"
#include "lib/shadow-shim-helper-rs/shadow_spinlock.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shim/patch_vdso.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_rdtsc.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_signals.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_syscall.h"
#include "lib/shim/shim_tls.h"
#include "main/host/syscall_numbers.h" // for SYS_shadow_* defs

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
ShimShmemThread* shim_threadSharedMem() { return _shim_thread_shared_mem_blk()->p; }

// Per-process state shared with Shadow.
static ShMemBlock* _shim_process_shared_mem_blk() {
    static ShMemBlock block = {0};
    return &block;
}
ShimShmemProcess* shim_processSharedMem() { return _shim_process_shared_mem_blk()->p; }

// Per-host state shared with Shadow.
static ShMemBlock* _shim_host_shared_mem_blk() {
    static ShMemBlock block = {0};
    return &block;
}
ShimShmemHost* shim_hostSharedMem() { return _shim_host_shared_mem_blk()->p; }

// We disable syscall interposition when this is > 0.
static int* _shim_allowNativeSyscallsFlag() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(bool));
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

void shim_newThreadStart(const ShMemBlockSerialized* block) {
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
    return old;
}

bool shim_interpositionEnabled() {
    return !*_shim_allowNativeSyscallsFlag();
}

static void** _shim_signal_stack() {
    static ShimTlsVar stack_var = {0};
    void** stack = shimtlsvar_ptr(&stack_var, sizeof(*stack));
    return stack;
}

// A signal stack waiting to be freed.
static void* free_signal_stack = NULL;

void shim_freeSignalStack() {
    // We can't free the current thread's signal stack, since
    // we may be running on it. Instead we save the pointer, so that
    // it can be freed later by another thread.

    if (free_signal_stack != NULL) {
        // First free the pending stack.
        if (free_signal_stack == *_shim_signal_stack()) {
            panic("Tried to free the current thread's signal stack twice");
        }
        munmap(free_signal_stack, SHIM_SIGNAL_STACK_SIZE);
    }
    free_signal_stack = *_shim_signal_stack();
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

    void* new_stack = mmap(NULL, stack_sz, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (new_stack == MAP_FAILED) {
        panic("mmap: %s", strerror(errno));
    }
    if (*_shim_signal_stack() != NULL) {
        panic("Allocated signal stack twice for current thread");
    }
    *_shim_signal_stack() = new_stack;

    // Set up a guard page.
    if (mprotect(new_stack, sysconf(_SC_PAGESIZE), PROT_NONE) != 0) {
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

    // Redirect logger to stdout (shadow sets stdout and stderr to the shim log).
    {
        // the FILE takes ownership of the fd, so give it its own fd
        int shimlog_fd = dup(STDOUT_FILENO);
        FILE* log_file = fdopen(shimlog_fd, "w");
        if (log_file == NULL) {
            panic("fdopen: %s", strerror(errno));
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

static void _shim_parent_init_host_shm() {
    *_shim_host_shared_mem_blk() = shmemserializer_globalBlockDeserialize(
        shimshmem_getProcessHostShmem(shim_processSharedMem()));
    assert(shim_hostSharedMem());
}

static void _shim_parent_init_process_shm() {
    *_shim_process_shared_mem_blk() =
        shmemserializer_globalBlockDeserialize(shimshmem_getProcessShmem(shim_threadSharedMem()));
    assert(shim_processSharedMem());
}

static void _shim_parent_init_thread_shm() {
    const char* shm_blk_buf = getenv("SHADOW_SHM_THREAD_BLK");
    assert(shm_blk_buf);

    bool err = false;
    ShMemBlockSerialized shm_blk_serialized = shmemblockserialized_fromString(shm_blk_buf, &err);

    *_shim_thread_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(shim_threadSharedMem());
}

static void _shim_child_init_thread_shm() {
    assert(!shim_threadSharedMem());
    ShMemBlockSerialized shm_blk_serialized;

    long rv = syscall(SYS_shadow_get_shm_blk, &shm_blk_serialized);
    if (rv != 0) {
        panic("shadow_get_shm_blk: %s", strerror(errno));
        abort();
    }

    *_shim_thread_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(shim_threadSharedMem());
}

static void _shim_parent_init_ipc() {
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
    *_shim_ipcDataBlk() = _startThread.childIpcBlk;
}

static void _shim_preload_only_child_ipc_wait_for_start_event() {
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

    shimevent_recvEventFromShadow(ipc, &event);
    assert(shimevent_getId(&event) == SHIM_EVENT_START);
}

static void _shim_ipc_wait_for_start_event() {
    assert(shim_thisThreadEventIPC());

    ShimEvent event;
    trace("waiting for start event on %p", shim_thisThreadEventIPC);
    shimevent_recvEventFromShadow(shim_thisThreadEventIPC(), &event);
    assert(shimevent_getId(&event) == SHIM_EVENT_START);
}

static void _shim_parent_init_seccomp() {
    shim_seccomp_init();
}

static void _shim_parent_init_rdtsc_emu() {
    shim_rdtsc_init();
}

// Sets the working directory. Should only need to be done for the first thread
// of the process.
//
// TODO: Instead use posix_spawn_file_actions_addchdir_np in the shadow process,
// which was added in glibc 2.29. Currently this is blocked on debian-10, which
// uses glibc 2.28.
static void _shim_parent_set_working_dir() {
    const char* path = getenv("SHADOW_WORKING_DIR");
    if (!path) {
        panic("SHADOW_WORKING_DIR not set");
    }
    if (chdir(path) != 0) {
        panic("chdir: %s", strerror(errno));
    }
}

static void _shim_parent_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    shim_install_hardware_error_handlers();
    patch_vdso((void*)getauxval(AT_SYSINFO_EHDR));
    _shim_parent_init_thread_shm();
    _shim_parent_init_process_shm();
    _shim_parent_init_host_shm();
    _shim_parent_init_logging();
    _shim_parent_set_working_dir();
    _shim_parent_init_ipc();
    _shim_init_signal_stack();
    _shim_parent_init_death_signal();
    _shim_ipc_wait_for_start_event();
    _shim_parent_init_memory_manager();
    _shim_parent_init_rdtsc_emu();
    _shim_parent_init_seccomp();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

static void _shim_child_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_preload_only_child_init_ipc();
    _shim_init_signal_stack();
    _shim_preload_only_child_ipc_wait_for_start_event();
    _shim_child_init_thread_shm();

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
        _shim_parent_init_preload();
        did_global_init = true;
        trace("Finished shim parent init");
    } else {
        _shim_child_init_preload();
        trace("Finished shim child init");
    }
}

void shim_ensure_init() { _shim_load(); }