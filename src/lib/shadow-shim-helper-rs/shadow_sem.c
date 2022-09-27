#include "shadow_sem.h"

#include "lib/logger/logger.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <semaphore.h>
#include <stdalign.h>
#include <stdbool.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static _Atomic(uint32_t)* _shadow_sem_futex_addr(shadow_sem_t* s) {
    static_assert(sizeof(s->_value) == 4, "futex must be exactly 4 bytes large");
    static_assert(alignof(typeof(s->_value)) >= 4, "futex must be >= 4 aligned");
    return &s->_value;
}

// Perform a wake operation on the futex in `s`.
static int _futex_wake(shadow_sem_t* s) {
    return (int)syscall(SYS_futex, _shadow_sem_futex_addr(s), FUTEX_WAKE, 1, NULL, NULL, 0);
}

// Perform a wait operation on the futex in `s`, with an absolute timeout.
static int _futex_wait_abs(shadow_sem_t* s, const struct timespec* timeout) {
    // Unlike FUTEX_WAIT, FUTEX_WAIT_BITSET uses an absolute timeout.
    return (int)syscall(SYS_futex, _shadow_sem_futex_addr(s), FUTEX_WAIT_BITSET, 0, timeout, NULL,
                        FUTEX_BITSET_MATCH_ANY);
}

int shadow_sem_init(shadow_sem_t* sem, int pshared, unsigned int _value) {
    if (_value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }

    *sem = (shadow_sem_t){
        ._value = ATOMIC_VAR_INIT(_value),
        ._nwaiters = ATOMIC_VAR_INIT(0),
    };

    return 0;
}

int shadow_sem_destroy(shadow_sem_t* sem) {
    // Nothing to do.
    return 0;
}

int shadow_sem_post(shadow_sem_t* sem) {
    uint32_t prev_value = atomic_load_explicit(&sem->_value, memory_order_relaxed);
    while (1) {
        if (prev_value >= SEM_VALUE_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        // We use memory_order_seq_cst to get a global total ordering of the
        // operations on `_nwaiters` together with the operations on `_value`.
        if (atomic_compare_exchange_weak_explicit(&sem->_value, &prev_value, prev_value + 1,
                                                  memory_order_seq_cst, memory_order_relaxed)) {
            break;
        }
    }

    // If we didn't see a futex value of 0, we never need to do a wakeup.  A
    // concurrent thread that's trying to wait on the semaphore can't end up
    // sleeping on a non-zero _value, as enforced by the futex operation.
    if (prev_value != 0) {
        return 0;
    }

    // If no threads are asleep on the futex, we don't need to do a wakeup
    // operation.  While there is some cost and complexity for tracking
    // `_nwaiters`, this gives about a 5% performance improvement in the phold
    // mezzo benchmark.
    uint32_t nwaiters = atomic_load_explicit(&sem->_nwaiters, memory_order_seq_cst);
    if (nwaiters == 0) {
        return 0;
    }

    int futex_rv = _futex_wake(sem);
    if (futex_rv < 0) {
        // This shouldn't happen, and there's no good way to recover.
        panic("futex_wake: %s", strerror(errno));
    }

    return 0;
}

int shadow_sem_trywait(shadow_sem_t* sem) {
    uint32_t prev_value = atomic_load_explicit(&sem->_value, memory_order_relaxed);
    while (1) {
        if (prev_value == 0) {
            errno = EAGAIN;
            return -1;
        }
        // We use memory_order_seq_cst to get a global total ordering of the
        // operations on `_nwaiters` together with the operations on `_value`.
        if (atomic_compare_exchange_weak_explicit(&sem->_value, &prev_value, prev_value - 1,
                                                  memory_order_seq_cst, memory_order_relaxed)) {
            break;
        }
    }
    return 0;
}

// `abs_timeout` may be NULL to specify no timeout.
static int _shadow_sem_timedwait(shadow_sem_t* sem, const struct timespec* abs_timeout) {
    uint32_t prev_value = atomic_load_explicit(&sem->_value, memory_order_relaxed);
    while (1) {
        if (prev_value == 0) {
            // Wait on the futex.

            // memory_order_seq_cst for global total ordering of with operations
            // on _value.
            uint32_t prev_nwaiters =
                atomic_fetch_add_explicit(&sem->_nwaiters, 1, memory_order_seq_cst);
            if (prev_nwaiters == UINT32_MAX) {
                panic("Unhandled %ud + 1 waiters on shadow_sem_t", prev_nwaiters);
            }

            // We use FUTEX_WAIT_BITSET instead of FUTEX_WAIT so that we can specify an absolute
            // timeout. See futex(2).
            //
            // We use memory_order_seq_cst to get a global total ordering of the
            // operations on `_nwaiters` together with the operations on `_value`.
            int futex_res = _futex_wait_abs(sem, abs_timeout);
            atomic_fetch_sub_explicit(&sem->_nwaiters, 1, memory_order_seq_cst);
            if (futex_res < 0 && errno != EAGAIN) {
                // Propagate errno from the futex operation. Notably if the operation timed out,
                // errno will already be ETIMEDOUT.
                return -1;
            }
            // We either failed to sleep on the futex because the value had
            // already changed, or there was a futex wake operation. Either way,
            // get the current value and try again.
            prev_value = atomic_load_explicit(&sem->_value, memory_order_relaxed);
            continue;
        }
        // Try to take one
        if (atomic_compare_exchange_weak_explicit(&sem->_value, &prev_value, prev_value - 1,
                                                  memory_order_seq_cst, memory_order_relaxed)) {
            return 0;
        }
    }
}

int shadow_sem_timedwait(shadow_sem_t* sem, const struct timespec* abs_timeout) {
    return _shadow_sem_timedwait(sem, abs_timeout);
}

int shadow_sem_wait(shadow_sem_t* sem) { return _shadow_sem_timedwait(sem, NULL); }