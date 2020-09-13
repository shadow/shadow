/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_FUTEX_H_
#define SRC_MAIN_HOST_FUTEX_H_

#include <stdint.h>
#include <stdbool.h>

#include "main/host/thread.h"

// The states that a thread can be in on a given futex
typedef enum _FutexState FutexState;
enum _FutexState {
    FUTEX_STATE_NONE,     // Not registered on this futex
    FUTEX_STATE_WAITING,  // Waiting for a wakeup
    FUTEX_STATE_TIMEDOUT, // Timeout occurred while waiting
    FUTEX_STATE_WOKEUP,   // Wakeup occurred while waiting
};

// Opaque futex object.
typedef struct _Futex Futex;

// Create a new futex object using the unique address as the futex word.
Futex* futex_new(uint32_t* word);

// Increment the reference count for the futex.
void futex_ref(Futex* futex);
// Decrement the reference count for the futex and free it if the count reaches 0.
void futex_unref(Futex* futex);
void futex_unref_func(void* futex);

// Register the thread to wait for a wakeup on this futex.
void futex_wait(Futex* futex, Thread* thread, const struct timespec* timeout);
// Wakeup at most the given number of threads waiting on this futex; return the number of threads that were woken up.
unsigned int futex_wake(Futex* futex, unsigned int numWakeups);

// Checks the state of the thread on this futex given current conditions.
// FUTEX_STATE_NONE: the thread is not registered with this futex
// FUTEX_STATE_WAITING: the thread is registered and is still waiting for a wakeup
// FUTEX_STATE_TIMEDOUT: the thread was waiting, but a timeout has occurred and we have now
//                       deregistered the thread from this futex (calling checkState() again will
//                       return FUTEX_STATE_NONE).
// FUTEX_STATE_WOKEUP: the thread was waiting, but a wakeup has occurred and we have now
//                     deregistered the thread from this futex (calling checkState() again will
//                     return FUTEX_STATE_NONE).
FutexState futex_checkState(Futex* futex, Thread* thread);

// Return the value stored in the futex word.
uint32_t futex_getValue(Futex* futex);
// Return the unique address of this futex.
uint32_t* futex_getAddress(Futex* futex);
// Return true if no threads are operating on this futex.
bool futex_isEmpty(Futex* futex);

#endif /* SRC_MAIN_HOST_FUTEX_H_ */
