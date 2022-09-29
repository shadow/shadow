#include "shadow_spinlock.h"

#include <assert.h>
#include <sys/syscall.h>
#include <unistd.h>

int shadow_spin_init(shadow_spinlock_t* lock) {
    assert(lock);
    *lock = (shadow_spinlock_t){
        ._locked = false,
    };
    return 0;
}

int shadow_spin_lock(shadow_spinlock_t* lock) {
    assert(lock);
    while (1) {
        bool prev_locked = atomic_load_explicit(&lock->_locked, memory_order_relaxed);
        if (!prev_locked &&
            atomic_compare_exchange_weak_explicit(
                &lock->_locked, &prev_locked, true, memory_order_acquire, memory_order_relaxed)) {
            break;
        }
        // When this is used from the shim of a Shadow-managed process, we want
        // to avoid going through the normal syscall logic, which *could* end up
        // inadvertently recursing.
        //
        // The shim's seccomp policy allows sched_yield, so using a raw syscall
        // instruction works.
        //
        // TODO: create a (public?) helper for making native syscalls in
        // general, perhaps by setting a high-bit in the syscall number when
        // running under Shadow, which could be recognized by the shim's seccomp
        // filter and allowed to pass (after removing the extra bit).
        // Previously we called the shim's `shim_native_syscall` here, but that
        // requires to link against the shim library, which would be a circular
        // dependency.
        __asm__ __volatile__("syscall"
                            :
                            : "a"(SYS_sched_yield)
                            : "rcx", "r11", "memory");
    }
    return 0;
}

int shadow_spin_unlock(shadow_spinlock_t* lock) {
    assert(lock);
    assert(atomic_load_explicit(&lock->_locked, memory_order_relaxed));
    atomic_store_explicit(&lock->_locked, false, memory_order_release);
    return 0;
}