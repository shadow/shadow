/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/eventd.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "support/logger/logger.h"

struct _EventD {
    Descriptor super;

    uint64_t counter;
    bool is_closed;
    bool is_semaphore;

    MAGIC_DECLARE;
};

static EventD* _eventfd_fromDescriptor(Descriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_EVENTD);
    return (EventD*)descriptor;
}

static gboolean _eventd_close(Descriptor* descriptor) {
    EventD* eventd = _eventfd_fromDescriptor(descriptor);
    MAGIC_ASSERT(eventd);

    debug("event fd %i closing now", eventd->super.handle);
    
    eventd->is_closed = true;
    descriptor_adjustStatus(&(eventd->super), STATUS_DESCRIPTOR_ACTIVE, FALSE);
    
    if (eventd->super.handle > 0) {
        return TRUE; // deregister from process
    } else {
        return FALSE; // we are not owned by a process
    }
}

static void _eventd_free(Descriptor* descriptor) {
    EventD* eventd = _eventfd_fromDescriptor(descriptor);
    MAGIC_ASSERT(eventd);
    
    descriptor_clear((Descriptor*)eventd);
    MAGIC_CLEAR(eventd);
    
    free(eventd);
    worker_countObject(OBJECT_TYPE_EVENTD, COUNTER_TYPE_FREE);
}

static DescriptorFunctionTable _eventdFunctions = {
    _eventd_close, _eventd_free, MAGIC_VALUE};

static void _eventd_updateStatus(EventD* eventd) {
    // Set the descriptor as readable if we have a non-zero counter.
    descriptor_adjustStatus(&eventd->super, STATUS_DESCRIPTOR_READABLE, 
            eventd->counter > 0 ? 1 : 0);
    // Set the descriptor as writable if we can write a value of at least 1.
    descriptor_adjustStatus(&eventd->super, STATUS_DESCRIPTOR_WRITABLE, 
            eventd->counter < UINT64_MAX - 1 ? 1 : 0);
}

EventD* eventd_new(unsigned int counter_init_val, bool is_semaphore) {
    EventD* eventd = malloc(sizeof(*eventd));
    *eventd = (EventD){.counter = (uint64_t) counter_init_val, .is_semaphore = is_semaphore, MAGIC_INITIALIZER};

    descriptor_init(&(eventd->super), DT_EVENTD, &_eventdFunctions);
    descriptor_adjustStatus(&(eventd->super), STATUS_DESCRIPTOR_ACTIVE, TRUE);

    worker_countObject(OBJECT_TYPE_EVENTD, COUNTER_TYPE_NEW);
    _eventd_updateStatus(eventd);

    return eventd;
}

ssize_t eventd_read(EventD* eventd, void* buf, size_t buflen) {
    MAGIC_ASSERT(eventd);

    debug("Trying to read %zu bytes from event fd %i with counter %lu",
            buflen, eventd->super.handle, (long unsigned int) eventd->counter);

    if(buflen < sizeof(uint64_t)) {
        debug("Reading from eventd requires buffer of at least 8 bytes");
        return -EINVAL;
    }

    if(eventd->counter == 0) {
        debug("Eventd counter is 0 and cannot be read right now");
        return -EWOULDBLOCK;
    }

    // Behavior defined in `man eventfd`
    if(eventd->is_semaphore) {
        const uint64_t one = 1;
        memcpy(buf, &one, sizeof(uint64_t));
        eventd->counter--;
    } else {
        memcpy(buf, &(eventd->counter), sizeof(uint64_t));
        eventd->counter = 0;
    }

    _eventd_updateStatus(eventd);
    
    // successfully read the counter update value
    return sizeof(uint64_t);
}

ssize_t eventd_write(EventD* eventd, const void* buf, size_t buflen) {
    MAGIC_ASSERT(eventd);
    
    debug("Trying to write %zu bytes to event fd %i with counter %lu",
            buflen, eventd->super.handle, (long unsigned int) eventd->counter);
    
    if(buflen < sizeof(uint64_t)) {
        debug("Writing to eventd requires a buffer with at least 8 bytes");
        return -EINVAL;
    }

    uint64_t value;
    memcpy(&value, buf, sizeof(uint64_t));
    
    if(value == UINT64_MAX) {
        debug("We do not allow writing the max counter value");
        return -EINVAL;
    }
    
    const uint64_t max_allowed = UINT64_MAX - 1;
    if(value > max_allowed - eventd->counter) {
        debug("The write value does not currently fit into the counter");
        return -EWOULDBLOCK;
    } else {
        eventd->counter += value;
    }

    _eventd_updateStatus(eventd);

    // successfully wrote the counter update value
    return sizeof(uint64_t);
}
