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

#include <sched.h>

#include "shim/ipc.h"
#include "shim/shim_event.h"
#include "shim/shim_logger.h"
#include "support/logger/logger.h"

typedef struct _TIDBlkPair {
    pthread_t tid;
    ShMemBlock ipc_blk;
} TIDBlkPair;

static pthread_mutex_t tid_fd_tree_mtx = PTHREAD_MUTEX_INITIALIZER;
static void *tid_fd_tree = NULL;

// Whether Shadow is using INTERPOSE_PRELOAD
static bool _using_interpose_preload;

// We disable syscall interposition when this is > 0.
static __thread int _shim_disable_interposition = 0;

static void _shim_wait_start(ShMemBlock *ipc_blk);

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

static int _shim_tidBlkTreeCompare(const void *lhs, const void *rhs) {
    const TIDBlkPair *lhs_tidfd = lhs, *rhs_tidfd = rhs;
    return (lhs_tidfd->tid > rhs_tidfd->tid) - (lhs_tidfd->tid < rhs_tidfd->tid);
}

static void _shim_tidBlkTreeAdd(TIDBlkPair *tid_fd) {
    pthread_mutex_lock(&tid_fd_tree_mtx);

    tsearch(tid_fd, &tid_fd_tree, _shim_tidBlkTreeCompare);

    pthread_mutex_unlock(&tid_fd_tree_mtx);
}

static TIDBlkPair _shim_tidBlkTreeGet(pthread_t tid) {
    pthread_mutex_lock(&tid_fd_tree_mtx);

    TIDBlkPair tid_fd, needle, **p = NULL;
    memset(&tid_fd, 0, sizeof(TIDBlkPair));
    memset(&needle, 0, sizeof(TIDBlkPair));

    needle.tid = tid;
    p = tfind(&needle, &tid_fd_tree, _shim_tidBlkTreeCompare);
    if (p != NULL) {
        tid_fd = *(*p);
    }

    pthread_mutex_unlock(&tid_fd_tree_mtx);

    return tid_fd;
}

__attribute__((constructor(SHIM_CONSTRUCTOR_PRIORITY))) static void
_shim_load() {
    shim_disableInterposition();

    struct sched_param sparam = {0};
    sparam.sched_priority = 1;
    // sched_setscheduler(0, SCHED_FIFO, &sparam);

    // We ultimately want to log to SHADOW_LOG_FILE, but we must temporarily
    // override the default logger with one that has a recursion-guard before
    // making any syscalls (e.g. to open the log file).
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

    ShMemBlock ipc_blk =
        shmemserializer_globalBlockDeserialize(&ipc_blk_serialized);

    pthread_mutex_init(&tid_fd_tree_mtx, NULL);

    TIDBlkPair *tid_blk = calloc(1, sizeof(TIDBlkPair));
    assert(tid_blk);

    tid_blk->tid = pthread_self();
    tid_blk->ipc_blk = ipc_blk;

    _shim_tidBlkTreeAdd(tid_blk);

    _shim_wait_start(&ipc_blk);

    debug("starting main");
    shim_enableInterposition();
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

    pthread_mutex_destroy(&tid_fd_tree_mtx);

    if (tid_fd_tree != NULL) {
        tdestroy(tid_fd_tree, free);
    }
    // Leave interposition disabled; shadow is waiting for
    // this process to die and won't listen to the shim pipe anymore.
}

static void _shim_wait_start(ShMemBlock *ipc_blk) {
    ShimEvent event;
    debug("waiting for start event on %p", ipc_blk->p);
    shimevent_recvEventFromShadow(ipc_blk->p, &event);
    assert(event.event_id == SHD_SHIM_EVENT_START);
    shimlogger_set_simulation_nanos(event.event_data.start.simulation_nanos);
}

ShMemBlock shim_thisThreadEventIPCBlk() {
    return _shim_tidBlkTreeGet(pthread_self()).ipc_blk;
}

