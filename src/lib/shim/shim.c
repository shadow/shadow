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
#include "lib/shim/shim_insn_emu.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_syscall.h"

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

static void _shim_parent_init_seccomp() {
    shim_seccomp_init();
}

static void _shim_parent_init_insn_emu() {
    shim_insn_emu_init();
}

void _shim_parent_init_preload() {
    if (shim_getExecutionContext() != EXECUTION_CONTEXT_SHADOW) {
        panic("Unexpectedly called from non-shadow context");
    }

    _shim_parent_init_ipc();
    _shim_ipc_wait_for_start_event();

    shim_install_hardware_error_handlers();
    patch_vdso((void*)getauxval(AT_SYSINFO_EHDR));
    _shim_parent_init_host_shm();
    _shim_parent_init_manager_shm();
    _shim_parent_init_logging();
    _shim_init_signal_stack();
    _shim_init_death_signal();
    _shim_parent_init_insn_emu();
    _shim_parent_init_seccomp();
    _shim_parent_close_stdin();
    preempt_process_init();
}

void _shim_child_thread_init_preload() {
    if (shim_getExecutionContext() != EXECUTION_CONTEXT_SHADOW) {
        panic("Unexpectedly called from non-shadow context");
    }

    _shim_preload_only_child_ipc_wait_for_start_event();

    _shim_init_signal_stack();
}

void _shim_child_process_init_preload() {
    if (shim_getExecutionContext() != EXECUTION_CONTEXT_SHADOW) {
        panic("Unexpectedly called from non-shadow context");
    }

    _shim_preload_only_child_ipc_wait_for_start_event();
    _shim_init_signal_stack();
    _shim_init_death_signal();
}

void shim_ensure_init() { _shim_load(); }
