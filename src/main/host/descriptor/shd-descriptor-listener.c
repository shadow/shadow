/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/shd-descriptor-listener.h"

#include <glib.h>
#include <stdlib.h>

#include "main/utility/utility.h"

struct _DescriptorListener {
    /* Specifies the type of descriptor status we want to wait for. */
    DescriptorStatus events;

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

static gboolean _descriptorlistener_shouldNotify(DescriptorListener* listener,
                                                 DescriptorStatus changed) {
    MAGIC_ASSERT(listener);
    /* Return TRUE if any status bits are set that match our listening bits. */
    return listener->events & changed;
}

static void _descriptorlistener_invokeNotifyFunc(DescriptorListener* listener) {
    MAGIC_ASSERT(listener);

    /* Trigger the callback function. */
    if (listener->notifyFunc) {
        listener->notifyFunc(
            listener->callbackObject, listener->callbackArgument);
    }
}

void descriptorlistener_onStatusChanged(DescriptorListener* listener,
                                        DescriptorStatus current,
                                        DescriptorStatus changed) {
    MAGIC_ASSERT(listener);

    if (_descriptorlistener_shouldNotify(listener, changed)) {
        _descriptorlistener_invokeNotifyFunc(listener);
    }
}

void descriptorlistener_setEvents(DescriptorListener* listener,
                                  DescriptorStatus events) {
    MAGIC_ASSERT(listener);
    listener->events = events;
}
