/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/shd-descriptor-listener.h"

#include <glib.h>
#include <stdlib.h>

#include "main/utility/utility.h"

struct _DescriptorListener {
    /* The descriptor status bits we want to monitor for transitions. */
    DescriptorStatus monitoring;

    /* The callback function to trigger. */
    DescriptorStatusCallbackFunc notifyFunc;
    /* The first argument to pass to the callback function. */
    gpointer callbackObject;
    /* The function we call to free the callback object. */
    DescriptorStatusObjectFreeFunc objectFreeFunc;
    /* The second argument to pass to the callback function. */
    gpointer callbackArgument;
    /* The function we call to free the callback argument. */
    DescriptorStatusArgumentFreeFunc argumentFreeFunc;

    /* Memory accounting. */
    int referenceCount;
    MAGIC_DECLARE;
};

DescriptorListener* descriptorlistener_new(
    DescriptorStatusCallbackFunc notifyFunc, void* callbackObject,
    DescriptorStatusObjectFreeFunc objectFreeFunc, void* callbackArgument,
    DescriptorStatusArgumentFreeFunc argumentFreeFunc) {
    DescriptorListener* listener = malloc(sizeof(DescriptorListener));

    *listener = (DescriptorListener){
        .notifyFunc = notifyFunc,
        .callbackObject = callbackObject,
        .objectFreeFunc = objectFreeFunc,
        .callbackArgument = callbackArgument,
        .argumentFreeFunc = argumentFreeFunc,
        .referenceCount = 1
    };

    MAGIC_INIT(listener);

    return listener;
}

static void _descriptorlistener_free(DescriptorListener* listener) {
    MAGIC_ASSERT(listener);

    if (listener->callbackObject && listener->objectFreeFunc) {
        listener->objectFreeFunc(listener->callbackObject);
    }

    if (listener->callbackArgument && listener->argumentFreeFunc) {
        listener->argumentFreeFunc(listener->callbackArgument);
    }

    MAGIC_CLEAR(listener);
    free(listener);
}

void descriptorlistener_ref(DescriptorListener* listener) {
    MAGIC_ASSERT(listener);
    listener->referenceCount++;
}

void descriptorlistener_unref(DescriptorListener* listener) {
    MAGIC_ASSERT(listener);
    listener->referenceCount--;
    utility_assert(listener->referenceCount >= 0);
    if (listener->referenceCount == 0) {
        _descriptorlistener_free(listener);
    }
}

/* Return TRUE if a transition (bit flip) occurred on any status bits that we
 * are monitoring.
 */
static gboolean _descriptorlistener_shouldNotify(DescriptorListener* listener,
                                                 DescriptorStatus transitions) {
    MAGIC_ASSERT(listener);
    return listener->monitoring & transitions;
}

/* Trigger the callback function. */
static void _descriptorlistener_invokeNotifyFunc(DescriptorListener* listener) {
    MAGIC_ASSERT(listener);

    if (listener->notifyFunc) {
        listener->notifyFunc(
            listener->callbackObject, listener->callbackArgument);
    }
}

void descriptorlistener_onStatusChanged(DescriptorListener* listener,
                                        DescriptorStatus transitions) {
    MAGIC_ASSERT(listener);

    if (_descriptorlistener_shouldNotify(listener, transitions)) {
        _descriptorlistener_invokeNotifyFunc(listener);
    }
}

void descriptorlistener_setMonitorStatus(DescriptorListener* listener,
                                         DescriptorStatus status) {
    MAGIC_ASSERT(listener);
    listener->monitoring = status;
}
