#include "binary_spinning_sem.h"

#include <cstring>
#include <unistd.h>

// (rwails) The number times we should increment the counter and
// check the atomic bool before we fall back to the semaphore.
// TODO: Move to an environment variable?
#define SHD_GATE_SPIN_MAX 8096

void BinarySpinningSem::init() {
    _x.store(false);
    _spin_ctr = 0;
    _thresh = SHD_GATE_SPIN_MAX;
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
    // Based loosely on
    // https://probablydance.com/2019/12/30/measuring-mutexes-spinlocks-and-how-bad-the-linux-scheduler-really-is/.
    if (spin) {
        while (_spin_ctr++ < _thresh) {
            bool was_available = _x.load(std::memory_order_relaxed);
            if (was_available &&
                _x.compare_exchange_weak(was_available, false, std::memory_order_acquire)) {
                break;
            }
            __asm__("pause"); // (rwails) Not sure if this op is helpful.
        }
    }
    sem_wait(&_semaphore);
    _spin_ctr = 0;
    pthread_spin_lock(&_lock);
    _x.store(false, std::memory_order_release);
    pthread_spin_unlock(&_lock);
}
