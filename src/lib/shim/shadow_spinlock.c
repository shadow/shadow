#include "lib/shim/shadow_spinlock.h"

#include <assert.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "lib/shim/preload_syscall.h"

int shadow_spin_init(shadow_spinlock_t *lock) {
    assert(lock);
    *lock = (shadow_spinlock_t) {
        ._locked = false,
    };
    return 0;
}

int shadow_spin_lock(shadow_spinlock_t* lock) {
    assert(lock);
    while (1) {
        bool prev_locked = atomic_load_explicit(&lock->_locked, memory_order_relaxed);
        if (!prev_locked && atomic_compare_exchange_weak_explicit(&lock->_locked, &prev_locked, true,
                                                        memory_order_acquire, memory_order_relaxed)) {
            break;
        }
        // Always make the real syscall
        shadow_real_raw_syscall(SYS_sched_yield);
    }
    return 0;
}

int shadow_spin_unlock(shadow_spinlock_t *lock) {
    assert(lock);
    assert(atomic_load_explicit(&lock->_locked, memory_order_relaxed));
    atomic_store_explicit(&lock->_locked, false, memory_order_release);
    return 0;
}