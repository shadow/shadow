#include "binary_spinning_sem.h"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <unistd.h>

#include <sched.h>

BinarySpinningSem::BinarySpinningSem(ssize_t spin_max) : _thresh(spin_max) {
    sem_init(&_semaphore, 1, 0);
}

void BinarySpinningSem::post() {
    sem_post(&_semaphore);
    sched_yield();
}

void BinarySpinningSem::wait(bool spin) {
    if (spin) {
        for (int i = 0; _thresh < 0 || i < _thresh; ++i) {
            if (sem_trywait(&_semaphore) == 0) {
                return;
            }
        }
    }
    sem_wait(&_semaphore);
}

int BinarySpinningSem::trywait() { return sem_trywait(&_semaphore); }
