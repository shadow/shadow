
#include <time.h>
#include <sys/eventfd.h>

#include "shadow.h"


struct _EventFD {
    Descriptor super;

    // from man eventfd
    // "The EventFD object contains an
    // unsigned 64-bit integer (uint64_t) counter that is maintained by the
    // kernel."
    guint64 count;

    MAGIC_DECLARE;
};

static void _eventfd_close(EventFD* eventfd) {
    MAGIC_ASSERT(eventfd);
//    eventfd->isClosed = TRUE;
    descriptor_adjustStatus(&(eventfd->super), DS_ACTIVE, FALSE);
    host_closeDescriptor(worker_getActiveHost(), eventfd->super.handle);
}

static void _eventfd_free(EventFD* eventfd) {
    MAGIC_ASSERT(eventfd);
    MAGIC_CLEAR(eventfd);
    g_free(eventfd);
    worker_countObject(OBJECT_TYPE_TIMER, COUNTER_TYPE_FREE);
}

static DescriptorFunctionTable _eventfdFunctions = {
        (DescriptorFunc) _eventfd_close,
        (DescriptorFunc) _eventfd_free,
        MAGIC_VALUE
};

EventFD* eventfd_new(gint handle, gint flags) {
//    if(clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC) {
//        errno = EINVAL;
//        return NULL;
//    }
//
//    if(flags != 0 && flags != TFD_NONBLOCK && flags != TFD_CLOEXEC
//       && flags != (TFD_NONBLOCK|TFD_CLOEXEC)) {
//        errno = EINVAL;
//        return NULL;
//    }

    EventFD* eventfd = g_new0(EventFD, 1);
    MAGIC_INIT(eventfd);

    descriptor_init(&(eventfd->super), DT_EVENTFD, &_eventfdFunctions, handle);
    descriptor_adjustStatus(&(eventfd->super), DS_ACTIVE, TRUE);

    worker_countObject(OBJECT_TYPE_TIMER, COUNTER_TYPE_NEW);

    return eventfd;
}

void eventfd_setInitVal(EventFD* eventfd, int initval) {
    eventfd->count = initval;
}

ssize_t shd_eventfd_read(EventFD* eventfd, void *buf, size_t count) {
    MAGIC_ASSERT(eventfd);
    // TODO : need to support blocking mode for eventfd
    // TODO : need to support EFD_SEMAPHORE flag

    //  quote from man eventfd
    //  "A read(2) fails with the error EINVAL if the size of the supplied buffer
    //  is less than 8 bytes."
    if(count < sizeof(guint64)) {
        errno = EINVAL;
        return (ssize_t) -1;
    }
    //*(uint64_t*)buf = eventfd->count;

    // quote from man eventfd
    // "If EFD_SEMAPHORE was not specified and the eventfd counter
    //                 has a nonzero value, then a read(2) returns 8 bytes
    //                 containing that value, and the counter's value is reset to
    //                 zero."

    if (eventfd->count == 0) {
        errno = EAGAIN;
        return (ssize_t) -1;
    }
    else {
        guint64 value = eventfd->count;
        *(uint64_t*)buf = value;
        eventfd->count = 0;
        descriptor_adjustStatus(&(eventfd->super), DS_READABLE, FALSE);
        return count;
    }
}

ssize_t shd_eventfd_write(EventFD* eventfd, const void *buf, size_t count) {
    MAGIC_ASSERT(eventfd);

    //  quote from man eventfd
    //  "A write(2) fails with the error EINVAL if the size of the
    //              supplied buffer is less than 8 bytes, or if an attempt is made
    //              to write the value 0xffffffffffffffff."
    if(count < sizeof(guint64)) {
        errno = EINVAL;
        return (ssize_t) -1;
    }

    guint64 value = *(uint64_t*)buf;

    if (value == 0xffffffffffffffff) {
        errno = EINVAL;
        return (ssize_t) -1;
    }
    else if (eventfd->count > 0xfffffffffffffffe - value) {
        // overflow
        // TODO : in blocking mode, write blocks until a read is performed on the descriptor
        errno = EAGAIN;
        return (ssize_t) -1;
    }
    else if (value == 0) {
        return count;
    }
    else {
        eventfd->count += value;
        descriptor_adjustStatus(&(eventfd->super), DS_READABLE, TRUE);
        return count;
    }
}