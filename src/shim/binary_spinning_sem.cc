#include "binary_spinning_sem.h"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <unistd.h>

BinarySpinningSem::BinarySpinningSem(ssize_t spin_max) : _thresh(spin_max) {
    _x.store(false);
    sem_init(&_semaphore, 1, 0);
    pthread_spin_init(&_lock, PTHREAD_PROCESS_SHARED);
}

void BinarySpinningSem::post() {
    pthread_spin_lock(&_lock);
    sem_post(&_semaphore);
    _x.store(true, std::memory_order_release);
    pthread_spin_unlock(&_lock);
}

void BinarySpinningSem::wait(bool spin) {
    ssize_t spin_ctr = 0;
    // Based loosely on
    // https://probablydance.com/2019/12/30/measuring-mutexes-spinlocks-and-how-bad-the-linux-scheduler-really-is/.
    if (spin) {
        while (_thresh < 0 || spin_ctr++ < _thresh) {
            bool was_available = _x.load(std::memory_order_relaxed);
            if (was_available &&
                _x.compare_exchange_weak(was_available, false, std::memory_order_acquire)) {
                break;
            }
            __asm__("pause"); // (rwails) Not sure if this op is helpful.
        }
    }
    sem_wait(&_semaphore);
    pthread_spin_lock(&_lock);
    _x.store(false, std::memory_order_release);
    pthread_spin_unlock(&_lock);
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
