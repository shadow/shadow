#include "main/utility/fork_proxy.h"

#include <errno.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "main/core/logger/shadow_logger.h"
#include "support/logger/logger.h"

struct _ForkProxy {
    pid_t (*do_fork_exec)(const char* file, char* const argv[], char* const envp[]);

    // Thread that will fork the requested processes.
    pthread_t pthread;

    // Post to initiate a fork request.
    sem_t sem_begin;
    // Posts to signal request completion.
    sem_t sem_done;

    // Request arguments.
    const char* file;
    char* const* argv;
    char* const* envp;

    // Request result.
    pid_t child_pid;
};

static int _sem_wait_ignoring_interrupts(sem_t* sem) {
    int rv;
    do {
        rv = sem_wait(sem);
    } while (rv == -1 && errno == EINTR);
    return rv;
}

// Function executed by a ForkProxy thread.
void* forkproxy_fn(void* void_forkproxy) {
    ForkProxy* forkproxy = void_forkproxy;
    shadow_logger_register(shadow_logger_getDefault(), pthread_self());

    while (1) {
        // Wait for a request.
        if (_sem_wait_ignoring_interrupts(&forkproxy->sem_begin) != 0) {
            error("sem_wait: %s", g_strerror(errno));
        }

        forkproxy->child_pid =
            forkproxy->do_fork_exec(forkproxy->file, forkproxy->argv, forkproxy->envp);

        // Signal calling thread that we're done.
        sem_post(&forkproxy->sem_done);
    }
}

ForkProxy* forkproxy_new(pid_t (*do_fork_exec)(const char* file, char* const argv[],
                                               char* const envp[])) {
    ForkProxy* forkproxy = malloc(sizeof(*forkproxy));
    *forkproxy = (ForkProxy){
        .do_fork_exec = do_fork_exec,
    };
    if (sem_init(&forkproxy->sem_begin, 0, 0) != 0) {
        error("sem_init: %s", g_strerror(errno));
    }
    if (sem_init(&forkproxy->sem_done, 0, 0) != 0) {
        error("sem_init: %s", g_strerror(errno));
    }
    int rv;
    if ((rv = pthread_create(&forkproxy->pthread, NULL, forkproxy_fn, forkproxy)) != 0) {
        error("pthread_create: %s", g_strerror(rv));
    }
    return forkproxy;
}

pid_t forkproxy_forkExec(ForkProxy* forkproxy, const char* file, char* const argv[],
                         char* const envp[]) {
    forkproxy->file = file;
    forkproxy->argv = argv;
    forkproxy->envp = envp;
    if (sem_post(&forkproxy->sem_begin) != 0) {
        error("sem_post: %s", g_strerror(errno));
    }
    if (_sem_wait_ignoring_interrupts(&forkproxy->sem_done) != 0) {
        error("sem_wait: %s", g_strerror(errno));
    }
    return forkproxy->child_pid;
}
