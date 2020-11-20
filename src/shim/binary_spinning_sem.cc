#include "binary_spinning_sem.h"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <unistd.h>

BinarySpinningSem::BinarySpinningSem(ssize_t spin_max) : _thresh(spin_max) {
    sem_init(&_semaphore, 1, 0);
}

void BinarySpinningSem::post() {
    sem_post(&_semaphore);
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

int BinarySpinningSem::trywait() {
    // Grab the `_x` spinlock first. If we don't, a concurrent thread calling
    // `wait` could break out of its spin loop early, only to have us grab the
    // semaphore out from under it.
    bool available;
    if (!((available = _x.load(std::memory_order_relaxed)) &&
          _x.compare_exchange_weak(available, false, std::memory_order_acquire))) {
        errno = EAGAIN;
        return -1;
    }

    int rv = sem_trywait(&_semaphore);
    if (rv != 0) {
        assert(rv == -1 && (errno == EAGAIN || errno == EINTR));
        // We can get here if a concurrent thread called `wait` and ended up
        // waiting on the semaphore without grabbing `_x` first (because it
        // chose not to spin or because it hit `_thresh`).
        //
        // The thread that successfully grabbed the semaphore is responsible
        // for resetting `_x` to false. (i.e. NOT this thread if we get here).
        return rv;
    }

    pthread_spin_lock(&_lock);
    _x.store(false, std::memory_order_release);
    pthread_spin_unlock(&_lock);
    return 0;
}
