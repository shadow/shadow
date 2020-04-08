/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_LISTENER_H_
#define SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_LISTENER_H_

#include "shd-descriptor-status.h"

/* Opaque object to store the state needed to implement the module. */
typedef struct _DescriptorListener DescriptorListener;

/* Function definitions used by the module. */
typedef void (*DescriptorStatusCallbackFunc)(void *callbackObject, void *callbackArgument);
typedef void (*DescriptorStatusObjectFreeFunc)(void *data);
typedef void (*DescriptorStatusArgumentFreeFunc)(void *data);

/* Create an object that can be set to listen to a descriptor's status
 * and execute a callback when the status includes the requested events
 * from setEvents. */
DescriptorListener* descriptorlistener_new(DescriptorStatusCallbackFunc notifyFunc,
        void *callbackObject, DescriptorStatusObjectFreeFunc objectFreeFunc,
        void *callbackArgument, DescriptorStatusArgumentFreeFunc argumentFreeFunc);

/* Increment the reference count for this listener. */
void descriptorlistener_ref(DescriptorListener* listener);
/* Decrement the reference count and free the listener if no refs remain. */
void descriptorlistener_unref(DescriptorListener* listener);

/* Called by the descriptor when it's status changes. This will
 * trigger a notification via the callback supplied to the new func
 * if the status of the descriptor matches the requested events. */
void descriptorlistener_onStatusChanged(DescriptorListener* listener,
        DescriptorStatus current, DescriptorStatus changed);

/* Set the requested events that we should listen for. */
void descriptorlistener_setEvents(DescriptorListener* listener, DescriptorStatus events);

#endif /* SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_LISTENER_H_ */
