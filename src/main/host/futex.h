/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_FUTEX_H_
#define SRC_MAIN_HOST_FUTEX_H_

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

// Opaque futex object.
typedef struct _Futex Futex;

#include "main/bindings/c/bindings-opaque.h"
#include "main/host/status_listener.h"

// Create a new futex object using the unique address as the futex word.
Futex* futex_new(ManagedPhysicalMemoryAddr word);

// Increment the reference count for the futex.
void futex_ref(Futex* futex);

// Decrement the reference count for the futex and free it if the count reaches 0.
void futex_unref(Futex* futex);
void futex_unref_func(void* futex);

// Return the unique address of this futex.
ManagedPhysicalMemoryAddr futex_getAddress(Futex* futex);

// Wakeup at most the given number of listener threads waiting on this futex; return the number of
// threads that were woken up.
unsigned int futex_wake(Futex* futex, unsigned int numWakeups);

// Add a listener that will be notified when a wakup occurs
void futex_addListener(Futex* futex, StatusListener* listener);

// Remove a listener from those that are waiting for wakeups
void futex_removeListener(Futex* futex, StatusListener* listener);

// Return the number of listers currently awaiting a wakeup
unsigned int futex_getListenerCount(Futex* futex);

#endif /* SRC_MAIN_HOST_FUTEX_H_ */
