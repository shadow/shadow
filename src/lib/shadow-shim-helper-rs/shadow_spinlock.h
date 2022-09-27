#ifndef SHADOW_SPINLOCK_H
#define SHADOW_SPINLOCK_H

#include <stdatomic.h>
#include <stdbool.h>

// Provides a subset of the pthread_spinlock_t interface. Methods
// are guaranteed to never make syscalls other than sched_yield.
typedef struct {
    _Atomic bool _locked;
} shadow_spinlock_t;

#define SHADOW_SPINLOCK_STATICALLY_INITD ((shadow_spinlock_t){._locked = ATOMIC_VAR_INIT(false)})

int shadow_spin_init(shadow_spinlock_t* lock);
int shadow_spin_lock(shadow_spinlock_t* lock);
int shadow_spin_unlock(shadow_spinlock_t* lock);

#endif