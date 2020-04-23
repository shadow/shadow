/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_LISTENER_H_
#define SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_LISTENER_H_

#include "descriptor_status.h"

/* Opaque object to store the state needed to implement the module. */
typedef struct _DescriptorListener DescriptorListener;

/* Function definitions used by the module. */
typedef void (*DescriptorStatusCallbackFunc)(void* callbackObject,
                                             void* callbackArgument);
typedef void (*DescriptorStatusObjectFreeFunc)(void* data);
typedef void (*DescriptorStatusArgumentFreeFunc)(void* data);

/* Create an object that can be set to listen to a descriptor's status
 * and execute a callback whenever a state transition (bit flips) occurs
 * on one of the status bits that are requested in setMonitorStatus.
 * Note that the callback will never be called unless setMonitorStatus is first
 * used to specify which status bits this listener should monitor. */
DescriptorListener* descriptorlistener_new(
    DescriptorStatusCallbackFunc notifyFunc, void* callbackObject,
    DescriptorStatusObjectFreeFunc objectFreeFunc, void* callbackArgument,
    DescriptorStatusArgumentFreeFunc argumentFreeFunc);

/* Increment the reference count for this listener. */
void descriptorlistener_ref(DescriptorListener* listener);
/* Decrement the reference count and free the listener if no refs remain. */
void descriptorlistener_unref(DescriptorListener* listener);

/* Called by the descriptor when a transition (bit flip) occurred on
 * at least one of its status bits. (This function should only be called
 * by the descriptor base class.)
 * If this listener is monitoring (via setMonitorStatus) any of the status bits
 * that just transitioned, then this function will trigger a notification
 * via the callback supplied to the new func.*/
void descriptorlistener_onStatusChanged(DescriptorListener* listener,
                                        DescriptorStatus transitions);

/* Set the status bits that we should monitor for transitions (flips). */
void descriptorlistener_setMonitorStatus(DescriptorListener* listener,
                                         DescriptorStatus status);

#endif /* SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_LISTENER_H_ */
