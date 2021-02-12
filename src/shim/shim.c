#include "shim/shim.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <search.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "shim/ipc.h"
#include "shim/shim_event.h"
#include "shim/shim_logger.h"
#include "support/logger/logger.h"

// Whether Shadow is using preload-based interposition.
static bool _using_interpose_preload;

// Whether Shadow is using ptrace-based interposition.
static bool _using_interpose_ptrace;

// This thread's IPC block, for communication with Shadow.
static __thread ShMemBlock _shim_ipc_blk;

// Per-thread state shared with Shadow.
static __thread ShMemBlock _shim_shared_mem_blk;
static __thread ShimSharedMem* _shim_shared_mem = NULL;

// We disable syscall interposition when this is > 0.
static __thread int _shim_disable_interposition = 0;

static void _shim_wait_start();

void shim_disableInterposition() {
    if (++_shim_disable_interposition == 1 && _using_interpose_ptrace && _using_interpose_preload) {
        if (_shim_shared_mem) {
            _shim_shared_mem->ptrace_allow_native_syscalls = true;
            debug("enabled native-syscalls via shmem %p", _shim_shared_mem);
        } else {
            shadow_set_ptrace_allow_native_syscalls(true);
            debug("enabled native-syscalls via syscall");
        }
    }
}

void shim_enableInterposition() {
    assert(_shim_disable_interposition > 0);
    if (--_shim_disable_interposition == 0 && _using_interpose_ptrace && _using_interpose_preload) {
        if (_shim_shared_mem) {
            debug("disabling native-syscalls via shmem %p", _shim_shared_mem);
            _shim_shared_mem->ptrace_allow_native_syscalls = false;
        } else {
            debug("disabling native-syscalls via syscall");
            shadow_set_ptrace_allow_native_syscalls(false);
        }
    }
}

bool shim_interpositionEnabled() {
    return _using_interpose_preload && !_shim_disable_interposition;
}

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
    if (!strcmp(interpose_method, "PRELOAD_ONLY")) {
        _using_interpose_preload = true;
        return;
    }
    if (!strcmp(interpose_method, "PRELOAD_PTRACE")) {
        _using_interpose_preload = true;
        _using_interpose_ptrace = true;
        return;
    }
    if (!strcmp(interpose_method, "PTRACE_ONLY")) {
        // From the shim's point of view, behave as if it's not running under
        // Shadow, and let all control happen via ptrace.
        _using_interpose_ptrace = true;
        return;
    }
    abort();
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
            error("SHADOW_PID does not contain an unsigned: %s", shadow_pid_str);
        }
    }

    if (valid_parse_pid) {
        if (getppid() == shadow_pid) { // Validate that Shadow is still alive.
            debug("Plugin verified Shadow is still running as parent.");
        } else {
            error("Shadow exited.");
            exit(-1); // If Shadow's dead, we can just get out(?)
        }
    }
}

static void _shim_load() {
    if (!_using_interpose_preload) {
        return;
    }

    const char* ipc_blk_buf = getenv("SHADOW_IPC_BLK");
    assert(ipc_blk_buf);
    bool err = false;
    ShMemBlockSerialized ipc_blk_serialized = shmemblockserialized_fromString(ipc_blk_buf, &err);
    assert(!err);

    _shim_ipc_blk = shmemserializer_globalBlockDeserialize(&ipc_blk_serialized);

    // Set logger start time from environment variable.
    {
        const char* logger_start_time_string = getenv("SHADOW_LOG_START_TIME");
        assert(logger_start_time_string);
        int64_t logger_start_time;
        assert(sscanf(logger_start_time_string, "%" PRId64, &logger_start_time) == 1);
        logger_set_global_start_time_micros(logger_start_time);
    }

    // Redirect logger to specified log file. The shim logger internally
    // disables interposition while logging, so we open the log file with
    // interposition disabled to get a native file descriptor.
    //
    // At this time, shim_disableInterposition *doesn't* prevent
    // ptrace-interposition from interposing, so when using ptrace-interposition
    // this actually *will* be interposed and we'll get a shadow file
    // descriptor. That's ok since the writes inside the logger will likewise be
    // interposed.
    {
        const char* name = getenv("SHADOW_LOG_FILE");
        FILE* log_file = fopen(name, "w");
        if (log_file == NULL) {
            perror("fopen");
            abort();
        }
        logger_setDefault(shimlogger_new(log_file));
    }

    // Ensure that the child process exits when Shadow does.  Shadow
    // ought to have already tried to terminate the child via SIGTERM
    // before shutting down (though see
    // https://github.com/shadow/shadow/issues/903), so now we jump all
    // the way to SIGKILL.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
        warning("prctl: %s", strerror(errno));
    }

    _verify_parent_pid_or_exit();

    debug("Finished shim global init");
}

// This function should be called before any wrapped syscall. We also use the
// constructor attribute to be completely sure that it's called before main.
__attribute__((constructor)) void shim_ensure_init() {
    static __thread bool started_thread_init = false;
    if (started_thread_init) {
        // Avoid deadlock when _shim_load's syscalls caused this function to be
        // called recursively.  In the uninitialized state,
        // `shim_interpositionEnabled` returns false, allowing _shim_load's
        // syscalls to execute natively.
        return;
    }
    started_thread_init = true;

    // We must set the interposition type before calling
    // shim_disableInterposition.
    _set_interpose_type();

    shim_disableInterposition();

    static bool did_global_init = false;
    if (!did_global_init) {
        _shim_load();
        did_global_init = true;
    }

    // If we're doing shim IPC, wait for the start event.
    if (_using_interpose_preload) {
        _shim_wait_start();
    }

    debug("Finished shim thread init");
    shim_enableInterposition();
}

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

    ShMemBlock ipc_blk = shim_thisThreadEventIPCBlk();
    ShimEvent shim_event;
    shim_event.event_id = SHD_SHIM_EVENT_STOP;
    debug("sending stop event on %p", ipc_blk.p);
    shimevent_sendEventToShadow(ipc_blk.p, &shim_event);

    // Leave interposition disabled; shadow is waiting for
    // this process to die and won't listen to the shim pipe anymore.
}

static void _shim_wait_start() {
    assert(_using_interpose_preload);

    // If we're using ptrace, and we haven't initialized the ipc block yet
    // (because this isn't the main thread, which is initialized in the global
    // initialization via an environment variable), do so.
    if (_using_interpose_ptrace && !_shim_ipc_blk.p) {
        ShMemBlockSerialized ipc_blk_serialized;
        int rv = shadow_get_ipc_blk(&ipc_blk_serialized);
        if (rv != 0) {
            error("shadow_get_ipc_blk: %s", strerror(errno));
            abort();
        }
        _shim_ipc_blk = shmemserializer_globalBlockDeserialize(&ipc_blk_serialized);
        assert(_shim_ipc_blk.p);
    }

    ShimEvent event;
    debug("waiting for start event on %p", _shim_ipc_blk.p);
    shimevent_recvEventFromShadow(_shim_ipc_blk.p, &event, /* spin= */ true);
    assert(event.event_id == SHD_SHIM_EVENT_START);
    shimlogger_set_simulation_nanos(event.event_data.start.simulation_nanos);
    if (_using_interpose_ptrace) {
        _shim_shared_mem_blk =
            shmemserializer_globalBlockDeserialize(&event.event_data.start.shim_shared_mem);
        _shim_shared_mem = _shim_shared_mem_blk.p;
        if (!_shim_shared_mem) {
            abort();
        }
    }
}

ShMemBlock shim_thisThreadEventIPCBlk() { return _shim_ipc_blk; }
