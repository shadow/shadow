#include "binary_spinning_sem.h"

#include <cstring>
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
