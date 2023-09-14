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
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shim/patch_vdso.h"
#include "lib/shim/shim_api.h"
#include "lib/shim/shim_rdtsc.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_syscall.h"
#include "main/host/syscall_numbers.h" // for SYS_shadow_* defs

static void _shim_parent_init_logging() {
    int level = shimshmem_getLogLevel(shim_hostSharedMem());

    // Route C logging through Rust's `log`
    logger_setDefault(rustlogger_new());
    // Install our `log` backend.
    shimlogger_install(level);
}

static void _shim_init_death_signal() {
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

static void _shim_parent_init_seccomp() {
    shim_seccomp_init();
}

static void _shim_parent_init_rdtsc_emu() {
    shim_rdtsc_init();
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
    _shim_init_signal_stack();
    _shim_init_death_signal();
    _shim_parent_init_memory_manager();
    _shim_parent_init_rdtsc_emu();
    _shim_parent_init_seccomp();
    _shim_parent_close_stdin();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

void _shim_child_thread_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_preload_only_child_ipc_wait_for_start_event();

    _shim_init_signal_stack();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

void _shim_child_process_init_preload() {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    _shim_preload_only_child_ipc_wait_for_start_event();
    _shim_init_signal_stack();
    _shim_init_death_signal();

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

void shim_ensure_init() { _shim_load(); }