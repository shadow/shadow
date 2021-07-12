#include "binary_spinning_sem.h"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <sched.h>
#include <unistd.h>

extern "C" {
#include "lib/logger/logger.h"
}

#include "lib/shim/shadow_sem.h"

BinarySpinningSem::BinarySpinningSem(ssize_t spin_max) : _thresh(spin_max) {
    shadow_sem_init(&_semaphore, 1, 0);
}

void BinarySpinningSem::post() {
    if (shadow_sem_post(&_semaphore)) {
        panic("shadow_sem: %s", strerror(errno));
    }
    sched_yield();
}

void BinarySpinningSem::wait(bool spin) {
    if (spin) {
        for (int i = 0; _thresh < 0 || i < _thresh; ++i) {
            if (shadow_sem_trywait(&_semaphore) == 0) {
                return;
            }
        }
    }
    if (shadow_sem_wait(&_semaphore)) {
        panic("shadow_sem: %s", strerror(errno));
    }
}

int BinarySpinningSem::trywait() { return shadow_sem_trywait(&_semaphore); }