#include "shim/shim.h"

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <search.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "shim/ipc.h"
#include "shim/shim_event.h"
#include "shim/shim_logger.h"
#include "support/logger/logger.h"

// Whether Shadow is using INTERPOSE_PRELOAD
static bool _using_interpose_preload;

// This thread's IPC block, for communication with Shadow.
static __thread ShMemBlock _shim_ipc_blk;

// We disable syscall interposition when this is > 0.
static __thread int _shim_disable_interposition = 0;

static void _shim_wait_start();

void shim_disableInterposition() {
    ++_shim_disable_interposition;
}

void shim_enableInterposition() {
    assert(_shim_disable_interposition > 0);
    --_shim_disable_interposition;
}

bool shim_interpositionEnabled() {
    return _using_interpose_preload && !_shim_disable_interposition;
}

static void _shim_load() {
    shim_disableInterposition();

    // We ultimately want to log to SHADOW_LOG_FILE, but first we redirect to
    // stderr for any log messages that happen before we can open it.
    logger_setDefault(shimlogger_new(stderr));

    // If we're not running under Shadow, return. This can be useful
    // for testing the libc parts of the shim.
    if (!getenv("SHADOW_SPAWNED")) {
        return;
    }

    // Set logger start time from environment variable.
    {
        const char* logger_start_time_string = getenv("SHADOW_LOG_START_TIME");
        assert(logger_start_time_string);
        int64_t logger_start_time;
        assert(sscanf(logger_start_time_string, "%" PRId64,
                      &logger_start_time) == 1);
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

    const char* interpose_method = getenv("SHADOW_INTERPOSE_METHOD");
    _using_interpose_preload =
        interpose_method != NULL && !strcmp(interpose_method, "PRELOAD");
    if (!_using_interpose_preload) {
        return;
    }

    const char *ipc_blk_buf = getenv("_SHD_IPC_BLK");
    assert(ipc_blk_buf);
    bool err = false;
    ShMemBlockSerialized ipc_blk_serialized =
        shmemblockserialized_fromString(ipc_blk_buf, &err);
    assert(!err);

    _shim_ipc_blk = shmemserializer_globalBlockDeserialize(&ipc_blk_serialized);
    _shim_wait_start();

    debug("starting main");
    shim_enableInterposition();
}

// This function should be called before any wrapped syscall. We also use the
// constructor attribute to be completely sure that it's called before main.
__attribute__((constructor)) void shim_ensure_init() {
    static __thread bool started_init = false;
    if (started_init) {
        // Avoid deadlock when _shim_load's syscalls caused this function to be
        // called recursively.  In the uninitialized state,
        // `shim_interpositionEnabled` returns false, allowing _shim_load's
        // syscalls to execute natively.
        return;
    }
    started_init = true;
    // Ensure that initialization only happens once globally, even if an earlier global constructor
    // created additional threads.
    static pthread_once_t _shim_init_once = PTHREAD_ONCE_INIT;
    pthread_once(&_shim_init_once, _shim_load);
}

__attribute__((destructor))
static void _shim_unload() {
    if (!_using_interpose_preload)
        return;
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
    ShimEvent event;
    debug("waiting for start event on %p", _shim_ipc_blk.p);
    shimevent_recvEventFromShadow(_shim_ipc_blk.p, &event, /* spin= */ true);
    assert(event.event_id == SHD_SHIM_EVENT_START);
    shimlogger_set_simulation_nanos(event.event_data.start.simulation_nanos);
}

ShMemBlock shim_thisThreadEventIPCBlk() { return _shim_ipc_blk; }
