#include "shim.h"
#include "shim_event.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <search.h>
#include <sys/types.h>

typedef struct _TIDFDPair {
    pthread_t tid;
    int fd;
} TIDFDPair;

static pthread_mutex_t tid_fd_tree_mtx = PTHREAD_MUTEX_INITIALIZER;
static void *tid_fd_tree = NULL;

static void _shim_wait_start(int event_fd);

static int _shim_tidFDTreeCompare(const void *lhs, const void *rhs) {
    const TIDFDPair *lhs_tidfd = lhs, *rhs_tidfd = rhs;
    return (lhs_tidfd->tid > rhs_tidfd->tid) - (lhs_tidfd->tid < rhs_tidfd->tid);
}

static void _shim_tidFDTreeAdd(TIDFDPair *tid_fd) {
    pthread_mutex_lock(&tid_fd_tree_mtx);

    tsearch(tid_fd, &tid_fd_tree, _shim_tidFDTreeCompare);

    pthread_mutex_unlock(&tid_fd_tree_mtx);
}

static TIDFDPair _shim_tidFDTreeGet(pthread_t tid) {
    pthread_mutex_lock(&tid_fd_tree_mtx);

    TIDFDPair tid_fd, needle, **p = NULL;
    memset(&tid_fd, 0, sizeof(TIDFDPair));
    memset(&needle, 0, sizeof(TIDFDPair));

    needle.tid = tid;
    p = tfind(&needle, &tid_fd_tree, _shim_tidFDTreeCompare);
    if (p != NULL) {
        tid_fd = *(*p);
    }

    pthread_mutex_unlock(&tid_fd_tree_mtx);

    return tid_fd;
}

static bool _using_interpose_preload;

// Priority must be higher than those in system-libc.c.
__attribute__((constructor(201))) static void _shim_load() {
    const char* interpose_method = getenv("SHADOW_INTERPOSE_METHOD");
    _using_interpose_preload =
        interpose_method != NULL && !strcmp(interpose_method, "PRELOAD");
    if (!_using_interpose_preload)
        return;
    const char *shd_event_sock_fd = getenv("_SHD_IPC_SOCKET");
    assert(shd_event_sock_fd);
    int event_sock_fd = atoi(shd_event_sock_fd);

    pthread_mutex_init(&tid_fd_tree_mtx, NULL);

    TIDFDPair *tid_fd = calloc(1, sizeof(TIDFDPair));
    assert(tid_fd != NULL);

    tid_fd->tid = pthread_self();
    tid_fd->fd = event_sock_fd;

    _shim_tidFDTreeAdd(tid_fd);

    SHD_SHIM_LOG("waiting for event on %d\n", event_sock_fd);
    _shim_wait_start(event_sock_fd);

    SHD_SHIM_LOG("starting main\n");
}

__attribute__((destructor))
static void _shim_unload() {
    if (!shim_usingInterposePreload())
        return;
    int event_fd = shim_thisThreadEventFD();

    ShimEvent shim_event;
    shim_event.event_id = SHD_SHIM_EVENT_STOP;
    SHD_SHIM_LOG("sending stop event on %d\n", event_fd);
    shimevent_sendEvent(event_fd, &shim_event);

    pthread_mutex_destroy(&tid_fd_tree_mtx);

    if (tid_fd_tree != NULL) {
        tdestroy(tid_fd_tree, free);
    }
}

static void _shim_wait_start(int event_fd) {
    ShimEvent event;

    shimevent_recvEvent(event_fd, &event);
    assert(event.event_id == SHD_SHIM_EVENT_START);
}

bool shim_usingInterposePreload() { return _using_interpose_preload; }

FILE *shim_logFD() {
    return stderr;
}

int shim_thisThreadEventFD() {
    return _shim_tidFDTreeGet(pthread_self()).fd;
}
