#include "binary_spinning_sem.h"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <unistd.h>

// (rwails) The number times we should increment the counter and
// check the atomic bool before we fall back to the semaphore.
// TODO: Move to an environment variable?
// FIXME: in hybrid mode, handle if this ends up blocking 
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

void BinarySpinningSem::wait() {
    bool expected = true;

    while (/*_spin_ctr++ < _thresh &&*/
           !_x.compare_exchange_weak(expected, false, std::memory_order_acquire)) {
        expected = true;
        __asm__("pause"); // (rwails) Not sure if this op is helpful.
    }

    sem_wait(&_semaphore);
    _spin_ctr = 0;

    pthread_spin_lock(&_lock);
    _x.store(false, std::memory_order_release);
    pthread_spin_unlock(&_lock);
}

int BinarySpinningSem::trywait() {
    int rv = sem_trywait(&_semaphore);
    if (rv != 0) {
        assert(rv == -1 && errno == EAGAIN);
        return rv;
    }

    _spin_ctr = 0;

    pthread_spin_lock(&_lock);
    _x.store(false, std::memory_order_release);
    pthread_spin_unlock(&_lock);
    return 0;
}
