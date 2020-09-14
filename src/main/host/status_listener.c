/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/status_listener.h"

#include <stdbool.h>
#include <stdlib.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/utility/utility.h"

struct _StatusListener {
    /* The descriptor status bits we want to monitor for transitions. */
    Status monitoring;
    /* A filter that specifies when we should trigger a callback. */
    StatusListenerFilter filter;

    /* The callback function to trigger. */
    StatusCallbackFunc notifyFunc;
    /* The first argument to pass to the callback function. */
    void* callbackObject;
    /* The function we call to free the callback object. */
    StatusObjectFreeFunc objectFreeFunc;
    /* The second argument to pass to the callback function. */
    void* callbackArgument;
    /* The function we call to free the callback argument. */
    StatusArgumentFreeFunc argumentFreeFunc;

    /* Memory accounting. */
    int referenceCount;
    MAGIC_DECLARE;
};

StatusListener* statuslistener_new(StatusCallbackFunc notifyFunc, void* callbackObject,
                                   StatusObjectFreeFunc objectFreeFunc, void* callbackArgument,
                                   StatusArgumentFreeFunc argumentFreeFunc) {
    StatusListener* listener = malloc(sizeof(StatusListener));

    *listener = (StatusListener){.notifyFunc = notifyFunc,
                                 .callbackObject = callbackObject,
                                 .objectFreeFunc = objectFreeFunc,
                                 .callbackArgument = callbackArgument,
                                 .argumentFreeFunc = argumentFreeFunc,
                                 .referenceCount = 1};

    MAGIC_INIT(listener);

    worker_countObject(OBJECT_TYPE_STATUS_LISTENER, COUNTER_TYPE_NEW);
    return listener;
}

static void _statuslistener_free(StatusListener* listener) {
    MAGIC_ASSERT(listener);

    if (listener->callbackObject && listener->objectFreeFunc) {
        listener->objectFreeFunc(listener->callbackObject);
    }

    if (listener->callbackArgument && listener->argumentFreeFunc) {
        listener->argumentFreeFunc(listener->callbackArgument);
    }

    MAGIC_CLEAR(listener);
    free(listener);
    worker_countObject(OBJECT_TYPE_STATUS_LISTENER, COUNTER_TYPE_FREE);
}

void statuslistener_ref(StatusListener* listener) {
    MAGIC_ASSERT(listener);
    listener->referenceCount++;
}

void statuslistener_unref(StatusListener* listener) {
    MAGIC_ASSERT(listener);
    listener->referenceCount--;
    utility_assert(listener->referenceCount >= 0);
    if (listener->referenceCount == 0) {
        _statuslistener_free(listener);
    }
}

/* Return TRUE if a transition (bit flip) occurred on any status bits that we
 * are monitoring.
 */
static bool _statuslistener_shouldNotify(StatusListener* listener, Status currentStatus,
                                         Status transitions) {
    MAGIC_ASSERT(listener);

    bool flipped = listener->monitoring & transitions;
    bool on = listener->monitoring & currentStatus;

    switch (listener->filter) {
        case SLF_OFF_TO_ON: return flipped && on;
        case SLF_ON_TO_OFF: return flipped && !on;
        case SLF_ALWAYS: return flipped;
        case SLF_NEVER:
        default: return 0;
    }
}

/* Trigger the callback function. */
static void _statuslistener_invokeNotifyFunc(StatusListener* listener) {
    MAGIC_ASSERT(listener);

    if (listener->notifyFunc) {
        listener->notifyFunc(listener->callbackObject, listener->callbackArgument);
    }
}

void statuslistener_onStatusChanged(StatusListener* listener, Status currentStatus,
                                    Status transitions) {
    MAGIC_ASSERT(listener);

    if (_statuslistener_shouldNotify(listener, currentStatus, transitions)) {
        _statuslistener_invokeNotifyFunc(listener);
    }
}

void statuslistener_setMonitorStatus(StatusListener* listener, Status status,
                                     StatusListenerFilter filter) {
    MAGIC_ASSERT(listener);
    listener->monitoring = status;
    listener->filter = filter;
}
