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

#include "lib/log-c2rust/log-c2rust.h"
#include "lib/log-c2rust/rustlogger.h"
#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shadow_sem.h"
#include "lib/shadow-shim-helper-rs/shadow_spinlock.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shim/patch_vdso.h"
#include "lib/shim/shim_api.h"
#include "lib/shim/shim_rdtsc.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_signals.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_syscall.h"
#include "main/host/syscall_numbers.h" // for SYS_shadow_* defs

// Per-process state shared with Shadow.
// Must remain valid for the lifetime of this process once initialized.
static ShMemBlock* _shim_process_shared_mem_blk() {
    static ShMemBlock block = {0};
    return &block;
}
const ShimShmemProcess* shim_processSharedMem() { return _shim_process_shared_mem_blk()->p; }

// Per-host state shared with Shadow.
// Must remain valid for the lifetime of this process once initialized.
static ShMemBlock* _shim_host_shared_mem_blk() {
    static ShMemBlock block = {0};
    return &block;
}
const ShimShmemHost* shim_hostSharedMem() { return _shim_host_shared_mem_blk()->p; }

// Held from the time of starting to initialize _startThread, to being done with
// it. i.e. ensure we don't try to start more than one thread at once.
//
// For example, this prevents the child thread, after having initialized itself
// and released the parent via the childInitd semaphore, from starting another
// clone itself until after the parent has woken up and released this lock.
static shadow_spinlock_t _startThreadLock = SHADOW_SPINLOCK_STATICALLY_INITD;
static struct {
    ShMemBlockSerialized childIpcBlk;
    shadow_sem_t childInitd;
} _startThread;

void shim_newThreadStart(const ShMemBlockSerialized* block) {
    if (shadow_spin_lock(&_startThreadLock)) {
        panic("shadow_spin_lock: %s", strerror(errno));
    };
    if (shadow_sem_init(&_startThread.childInitd, 0, 0)) {
        panic("shadow_sem_init: %s", strerror(errno));
    }
    _startThread.childIpcBlk = *block;
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
        if (shim_native_syscall(NULL, SYS_sched_yield)) {
            panic("shim_native_syscall(SYS_sched_yield): %s", strerror(errno));
        }
    }

    // Release the global clone lock.
    if (shadow_spin_unlock(&_startThreadLock)) {
        panic("shadow_spin_unlock: %s", strerror(errno));
    }
}

static void _shim_parent_init_logging() {
    int level = shimshmem_getLogLevel(shim_hostSharedMem());

    // Route C logging through Rust's `log`
    logger_setDefault(rustlogger_new());
    // Install our `log` backend.
    shimlogger_install(level);
}

static void _shim_parent_init_death_signal() {
    // Ensure that the child process exits when Shadow does. This is to avoid
    // confusing behavior or a "stalled out" process in the case that Shadow
    // exits abnormally. Shadow normally ensures all managed processes have
    // exited before exiting itself.
    //
    // TODO: This would be better to do in between (v)fork and exec, e.g. in
    // case the shim is never initialized properly, but isn't currently an
    // operation supported by posix_spawn.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
        warning("prctl: %s", strerror(errno));
    }

    // Exit now if Shadow has already exited before we made the above `prctl`
    // call.
    if (getppid() != shimshmem_getShadowPid(shim_hostSharedMem())) {
        error("Shadow exited.");
        exit(EXIT_FAILURE);
    }
}

static void _shim_parent_init_manager_shm() {
    _shim_set_manager_shmem(shimshmem_getHostManagerShmem(shim_hostSharedMem()));
    assert(shim_managerSharedMem());
}

static void _shim_parent_init_host_shm() {
    *_shim_host_shared_mem_blk() = shmemserializer_globalBlockDeserialize(
        shimshmem_getProcessHostShmem(shim_processSharedMem()));
    assert(shim_hostSharedMem());
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

static void _shim_preload_only_child_init_ipc() { _shim_set_ipc(&_startThread.childIpcBlk); }

static void _shim_preload_only_child_ipc_wait_for_start_event() {
    assert(shim_thisThreadEventIPC());

    trace("waiting for start event on %p", shim_thisThreadEventIPC());

    // We're returning control to the parent thread here, who is going to switch
    // back to their own TLS.
    const struct IPCData* ipc = shim_thisThreadEventIPC();

    // Releases parent thread, who switches back to their own TLS.  i.e. Don't
    // use TLS between here and when we can switch back to our own after
    // receiving the start event.
    shim_newThreadChildInitd();

    ShMemBlockSerialized thread_blk_serialized;
    ShimEventToShadow start_req;
    shimevent2shadow_initStartReq(&start_req, &thread_blk_serialized, NULL);
    shimevent_sendEventToShadow(ipc, &start_req);

    ShimEventToShim start_res;
    shimevent_recvEventFromShadow(ipc, &start_res);
    assert(shimevent2shim_getId(&start_res) == SHIM_EVENT_TO_SHIM_START_RES);

    _shim_set_thread_shmem(&thread_blk_serialized);
}

static void _shim_ipc_wait_for_start_event() {
    assert(shim_thisThreadEventIPC());

    trace("waiting for start event on %p", shim_thisThreadEventIPC());

    ShMemBlockSerialized thread_blk_serialized;
    ShMemBlockSerialized process_blk_serialized;
    ShimEventToShadow start_req;
    shimevent2shadow_initStartReq(&start_req, &thread_blk_serialized, &process_blk_serialized);
    shimevent_sendEventToShadow(shim_thisThreadEventIPC(), &start_req);

    ShimEventToShim start_res;
    shimevent_recvEventFromShadow(shim_thisThreadEventIPC(), &start_res);
    assert(shimevent2shim_getId(&start_res) == SHIM_EVENT_TO_SHIM_START_RES);

    _shim_set_thread_shmem(&thread_blk_serialized);
    *_shim_process_shared_mem_blk() =
        shmemserializer_globalBlockDeserialize(&process_blk_serialized);
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

void _shim_parent_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_parent_init_ipc();
    _shim_ipc_wait_for_start_event();

    shim_install_hardware_error_handlers();
    patch_vdso((void*)getauxval(AT_SYSINFO_EHDR));
    _shim_parent_init_host_shm();
    _shim_parent_init_manager_shm();
    _shim_parent_init_logging();
    _shim_parent_set_working_dir();
    _shim_init_signal_stack();
    _shim_parent_init_death_signal();
    _shim_parent_init_memory_manager();
    _shim_parent_init_rdtsc_emu();
    _shim_parent_init_seccomp();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

void _shim_child_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_preload_only_child_init_ipc();
    _shim_preload_only_child_ipc_wait_for_start_event();

    _shim_init_signal_stack();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

void shim_ensure_init() { _shim_load(); }