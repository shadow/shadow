#include "binary_spinning_sem.h"

#include <cstring>
#include <unistd.h>

// (rwails) The number times we should increment the counter and
// check the atomic bool before we fall back to the semaphore.
// TODO: Move to an environment variable?
#define SHD_GATE_SPIN_MAX 8096

void BinarySpinningSem::init() {
    _thresh = SHD_GATE_SPIN_MAX;
    sem_init(&_semaphore, 1, 0);
}

void BinarySpinningSem::post() {
    sem_post(&_semaphore);
}

void BinarySpinningSem::wait(bool spin) {
    for(int i=0; spin && i < _thresh; ++i) {
        if (sem_trywait(&_semaphore) == 0) {
            return;
        }
    }
    sem_wait(&_semaphore);
}
