/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/status_listener.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/utility/utility.h"

struct _StatusListener {
    /* The descriptor status bits we want to monitor for transitions. */
    FileState monitoring;
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

    /* Enables deterministic sorting of listener items. */
    uint64_t deterministicSequenceValue;

    /* Memory accounting. */
    int referenceCount;
    MAGIC_DECLARE;
};

int status_listener_compare(const void* ptr_1, const void* ptr_2) {
    const StatusListener* listener_1 = ptr_1;
    const StatusListener* listener_2 = ptr_2;

    if (listener_1->deterministicSequenceValue == listener_2->deterministicSequenceValue) {
        assert(listener_1 == listener_2);
        return 0;
    }

    return (listener_1->deterministicSequenceValue < listener_2->deterministicSequenceValue) ? -1 : 1;
}

StatusListener* statuslistener_new(StatusCallbackFunc notifyFunc, void* callbackObject,
                                   StatusObjectFreeFunc objectFreeFunc, void* callbackArgument,
                                   StatusArgumentFreeFunc argumentFreeFunc, const Host* host) {
    StatusListener* listener = malloc(sizeof(StatusListener));

    *listener =
        (StatusListener){.notifyFunc = notifyFunc,
                         .callbackObject = callbackObject,
                         .objectFreeFunc = objectFreeFunc,
                         .callbackArgument = callbackArgument,
                         .argumentFreeFunc = argumentFreeFunc,
                         .deterministicSequenceValue = host_getNextDeterministicSequenceValue(host),
                         .referenceCount = 1};

    MAGIC_INIT(listener);

    worker_count_allocation(StatusListener);
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
    worker_count_deallocation(StatusListener);
}

void statuslistener_ref(StatusListener* listener) {
    MAGIC_ASSERT(listener);
    listener->referenceCount++;
}

void statuslistener_unref(StatusListener* listener) {
    MAGIC_ASSERT(listener);
    listener->referenceCount--;
    utility_debugAssert(listener->referenceCount >= 0);
    if (listener->referenceCount == 0) {
        _statuslistener_free(listener);
    }
}

/* Return TRUE if a transition (bit flip) occurred on any status bits that we
 * are monitoring.
 */
static bool _statuslistener_shouldNotify(StatusListener* listener, FileState currentStatus,
                                         FileState transitions) {
    MAGIC_ASSERT(listener);

    bool flipped = listener->monitoring & transitions;
    bool on = listener->monitoring & currentStatus;

    switch (listener->filter) {
        case SLF_OFF_TO_ON: return flipped && on;
        case SLF_ON_TO_OFF: return flipped && !on;
        case SLF_ALWAYS: return flipped;
        case SLF_NEVER:
            return false;
            // no default to force compiler warning on unhandled types
    }

    // error if we didn't handle all possible filters
    utility_panic("Unhandled enumerator %d", listener->filter);
    return false;
}

/* Trigger the callback function. */
static void _statuslistener_invokeNotifyFunc(StatusListener* listener) {
    MAGIC_ASSERT(listener);

    if (listener->notifyFunc) {
        listener->notifyFunc(listener->callbackObject, listener->callbackArgument);
    }
}

void statuslistener_onStatusChanged(StatusListener* listener, FileState currentStatus,
                                    FileState transitions) {
    MAGIC_ASSERT(listener);

    if (_statuslistener_shouldNotify(listener, currentStatus, transitions)) {
        _statuslistener_invokeNotifyFunc(listener);
    }
}

void statuslistener_setMonitorStatus(StatusListener* listener, FileState status,
                                     StatusListenerFilter filter) {
    MAGIC_ASSERT(listener);
    listener->monitoring = status;
    listener->filter = filter;
}
