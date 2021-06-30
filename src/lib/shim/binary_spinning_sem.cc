#include "binary_spinning_sem.h"

#include "shadow_sem.h"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <sched.h>
#include <unistd.h>

BinarySpinningSem::BinarySpinningSem(ssize_t spin_max) : _thresh(spin_max) {
    shadow_sem_init(&_semaphore, 1, 0);
}

void BinarySpinningSem::post() {
    shadow_sem_post(&_semaphore);
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
    shadow_sem_wait(&_semaphore);
}

int BinarySpinningSem::trywait() { return shadow_sem_trywait(&_semaphore); }
