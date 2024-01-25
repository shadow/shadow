/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_STATUS_LISTENER_H_
#define SRC_MAIN_HOST_STATUS_LISTENER_H_

/* Opaque object to store the state needed to implement the module. */
typedef struct _StatusListener StatusListener;

#include "main/bindings/c/bindings-opaque.h"

/* Indicates when the listener should trigger a callback, i.e.,
 * when the status bits that we are monitoring flip from off to on,
 * from on to off, always (on any flip), or never. */
typedef enum _StatusListenerFilter StatusListenerFilter;
enum _StatusListenerFilter {
    SLF_NEVER,
    SLF_OFF_TO_ON,
    SLF_ON_TO_OFF,
    SLF_ALWAYS
};

/* Function definitions used by the module. */
typedef void (*StatusCallbackFunc)(void* callbackObject, void* callbackArgument);
typedef void (*StatusObjectFreeFunc)(void* data);
typedef void (*StatusArgumentFreeFunc)(void* data);

/* Create an object that can be set to listen to a status
 * and execute a callback whenever a state transition (bit flips) occurs
 * on one of the status bits that are requested in setMonitorStatus.
 * Note that the callback will never be called unless setMonitorStatus is first
 * used to specify which status bits this listener should monitor. */
StatusListener* statuslistener_new(StatusCallbackFunc notifyFunc, void* callbackObject,
                                   StatusObjectFreeFunc objectFreeFunc, void* callbackArgument,
                                   StatusArgumentFreeFunc argumentFreeFunc, const Host* host);

/* Increment the reference count for this listener. */
void statuslistener_ref(StatusListener* listener);
/* Decrement the reference count and free the listener if no refs remain. */
void statuslistener_unref(StatusListener* listener);

/* Called when a transition (bit flip) occurred on
 * at least one of its status bits. (This function should only be called
 * by status owners, i.e., the descriptor or futex base classes.)
 * If this listener is monitoring (via setMonitorStatus) any of the status bits
 * that just transitioned, then this function will trigger a notification
 * via the callback supplied to the new func.*/
void statuslistener_onStatusChanged(StatusListener* listener, FileState currentStatus,
                                    FileState transitions);

/* Set the status bits that we should monitor for transitions (flips),
 * and a filter that specifies which flips should cause the callback
 * to be invoked. */
void statuslistener_setMonitorStatus(StatusListener* listener, FileState status,
                                     StatusListenerFilter filter);

/* Used for sorting status listeners in GLib code. Returns -1 if the listener in
 * ptr_1 was created before the listener in ptr_2, +1 if the listener in ptr_2
 * was created before the listener in ptr_1, and 0 if the listener and their
 * pointer values are equal.  */
int status_listener_compare(const void* ptr_1, const void* ptr_2);

#endif /* SRC_MAIN_HOST_STATUS_LISTENER_H_ */
