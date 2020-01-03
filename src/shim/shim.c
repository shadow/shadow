#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include <pthread.h>
#include <search.h>
#include <sys/types.h>

typedef struct _TIDFDPair {
    pthread_t tid;
    int fd;
} TIDFDPair;

static pthread_mutex_t tid_fd_tree_mtx = PTHREAD_MUTEX_INITIALIZER;
static void *tid_fd_tree = NULL;

static int _shim_tidFDTreeCompare(const void *lhs, const void *rhs) {
    const TIDFDPair *lhs_tidfd = lhs, *rhs_tidfd = rhs;
    return (lhs_tidfd->tid > rhs_tidfd->tid) - (lhs_tidfd->tid < rhs_tidfd->tid);
}

static void _shim_tidFDTreeAdd(TIDFDPair *tid_fd) {
    pthread_mutex_lock(&tid_fd_tree_mtx);

    tsearch(tid_fd, tid_fd_tree, _shim_tidFDTreeCompare);

    pthread_mutex_unlock(&tid_fd_tree_mtx);
}

__attribute__((constructor)) static void _shim_load() {
    const char *ftm_event_sock_fd = getenv("_SHD_IPC_SOCKET");
    assert(ftm_event_sock_fd);
    int event_sock_fd = atoi(ftm_event_sock_fd);

    pthread_mutex_init(&tid_fd_tree_mtx, NULL);

    TIDFDPair *tid_fd = calloc(1, sizeof(TIDFDPair));
    assert(tid_fd != NULL);

    tid_fd->tid = pthread_self();
    tid_fd->fd = event_sock_fd;
}
