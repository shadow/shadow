/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/host/descriptor/epoll.h"

#include <errno.h>
#include <glib.h>
#include <string.h>
#include <sys/epoll.h>

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

typedef enum _EpollWatchFlags EpollWatchFlags;
enum _EpollWatchFlags {
    EWF_NONE = 0,
    /* the underlying shadow descriptor is initialized and operational */
    EWF_ACTIVE = 1 << 0,
    /* the underlying shadow descriptor is readable */
    EWF_READABLE = 1 << 1,
    /* the application is waiting for a read event on the underlying shadow descriptor */
    EWF_WAITINGREAD = 1 << 2,
    /* the readable status changed but the event has not yet been collected (for EDGETRIGGER) */
    EWF_READCHANGED = 1 << 3,
    /* the underlying shadow descriptor is writable */
    EWF_WRITEABLE = 1 << 4,
    /* the application is waiting for a write event on the underlying shadow descriptor */
    EWF_WAITINGWRITE = 1 << 5,
    /* the writable status changed but the event has not yet been collected (for EDGETRIGGER) */
    EWF_WRITECHANGED = 1 << 6,
    /* the underlying shadow descriptor is closed */
    EWF_CLOSED = 1 << 7,
    /* true if this watch is currently valid and in the watches table. this allows
     * support of lazy deletion of watches that are in the reportable queue when
     * we want to delete them, to avoid the O(n) removal time of the queue. */
    EWF_WATCHING = 1 << 8,
    /* set if edge-triggered events are enabled on the underlying shadow descriptor */
    EWF_EDGETRIGGER = 1 << 9,
    EWF_EDGETRIGGER_REPORTED = 1 << 10,
    /* set if one-shot events are enabled on the underlying shadow descriptor */
    EWF_ONESHOT = 1 << 11,
    /* used to track that ONESHOT mode is used, an event was already reported, and the
     * socket has not been modified since. This prevents duplicate reporting in ONESHOT mode. */
    EWF_ONESHOT_REPORTED = 1 << 12,
};

typedef struct _EpollWatch EpollWatch;
struct _EpollWatch {
    /* the shadow descriptor we are watching for events */
    LegacyDescriptor* descriptor;
    /* The listener that notifies us when status changes. */
    StatusListener* listener;
    /* holds the actual event info */
    struct epoll_event event;
    /* current status of the underlying shadow descriptor */
    EpollWatchFlags flags;
    gint referenceCount;
    MAGIC_DECLARE;
};

struct _Epoll {
    /* epoll itself is also a descriptor */
    LegacyDescriptor super;

    /* holds the wrappers for the descriptors we are watching for events */
    GHashTable* watching;

    /* holds the descriptors that we are watching that have events */
    GHashTable* ready;

    MAGIC_DECLARE;
};

/* forward declaration */
static void _epoll_descriptorStatusChanged(Epoll* epoll,
                                           LegacyDescriptor* descriptor);

static EpollWatch* _epollwatch_new(Epoll* epoll, LegacyDescriptor* descriptor,
                                   const struct epoll_event* event) {
    EpollWatch* watch = g_new0(EpollWatch, 1);
    MAGIC_INIT(watch);
    utility_assert(event);

    /* ref it for the EpollWatch, which also covers the listener reference
     * (which is freed below in _epollwatch_free) */
    descriptor_ref(descriptor);

    watch->descriptor = descriptor;
    watch->event = *event;
    watch->referenceCount = 1;

    /* Create the listener and ref the objects held by the listener.
     * The watch object already holds a ref to the descriptor so we
     * don't ref it again. */
    watch->listener = statuslistener_new(
        (StatusCallbackFunc)_epoll_descriptorStatusChanged, epoll, NULL, descriptor, NULL);

    return watch;
}

static void _epollwatch_free(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    statuslistener_unref(watch->listener);
    descriptor_unref(watch->descriptor);

    MAGIC_CLEAR(watch);
    g_free(watch);
}

static void _epollwatch_ref(EpollWatch* watch) {
    MAGIC_ASSERT(watch);
    (watch->referenceCount)++;
}

static void _epollwatch_unref(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    if(--(watch->referenceCount) <= 0) {
        _epollwatch_free(watch);
    }
}

static Epoll* _epoll_fromLegacyDescriptor(LegacyDescriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_EPOLL);
    return (Epoll*)descriptor;
}

/* should only be called from descriptor dereferencing the functionTable */
static void _epoll_free(LegacyDescriptor* descriptor) {
    Epoll* epoll = _epoll_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(epoll);

    /* this unrefs all of the remaining watches */
    g_hash_table_destroy(epoll->watching);
    g_hash_table_destroy(epoll->ready);

    descriptor_clear((LegacyDescriptor*)epoll);
    MAGIC_CLEAR(epoll);
    g_free(epoll);

    worker_countObject(OBJECT_TYPE_EPOLL, COUNTER_TYPE_FREE);
}

void epoll_clearWatchListeners(Epoll* epoll) {
    MAGIC_ASSERT(epoll);

    /* make sure none of our watch descriptors notify us anymore */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, epoll->watching);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        EpollWatch* watch = value;
        MAGIC_ASSERT(watch);
        statuslistener_setMonitorStatus(watch->listener, STATUS_NONE, SLF_NEVER);
        descriptor_removeListener(watch->descriptor, watch->listener);
    }
}

static gboolean _epoll_close(LegacyDescriptor* descriptor) {
    Epoll* epoll = _epoll_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(epoll);
    epoll_clearWatchListeners(epoll);
    return TRUE;
}

DescriptorFunctionTable epollFunctions = {
    _epoll_close, _epoll_free, MAGIC_VALUE};

Epoll* epoll_new() {
    Epoll* epoll = g_new0(Epoll, 1);
    MAGIC_INIT(epoll);

    descriptor_init(&(epoll->super), DT_EPOLL, &epollFunctions);

    /* allocate backend needed for managing events for this descriptor */
    epoll->watching = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)_epollwatch_unref);
    epoll->ready = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)_epollwatch_unref);

    /* the epoll descriptor itself is always able to be epolled */
    descriptor_adjustStatus(&(epoll->super), STATUS_DESCRIPTOR_ACTIVE, TRUE);

    worker_countObject(OBJECT_TYPE_EPOLL, COUNTER_TYPE_NEW);

    return epoll;
}

static void _epollwatch_updateStatus(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    /* store the old flags that are only lazily updated */
    EpollWatchFlags lazyFlags = 0;
    lazyFlags |= (watch->flags & EWF_READCHANGED) ? EWF_READCHANGED : EWF_NONE;
    lazyFlags |= (watch->flags & EWF_WRITECHANGED) ? EWF_WRITECHANGED : EWF_NONE;
    lazyFlags |= (watch->flags & EWF_WATCHING) ? EWF_WATCHING : EWF_NONE;
    lazyFlags |= (watch->flags & EWF_EDGETRIGGER_REPORTED) ? EWF_EDGETRIGGER_REPORTED : EWF_NONE;
    lazyFlags |= (watch->flags & EWF_ONESHOT_REPORTED) ? EWF_ONESHOT_REPORTED : EWF_NONE;

    /* reset our flags */
    EpollWatchFlags oldFlags = watch->flags;
    watch->flags = 0;

    /* check shadow descriptor status */
    Status status = descriptor_getStatus(watch->descriptor);
    watch->flags |= (status & STATUS_DESCRIPTOR_ACTIVE) ? EWF_ACTIVE : EWF_NONE;
    watch->flags |= (status & STATUS_DESCRIPTOR_READABLE) ? EWF_READABLE : EWF_NONE;
    watch->flags |= (status & STATUS_DESCRIPTOR_WRITABLE) ? EWF_WRITEABLE : EWF_NONE;
    watch->flags |= (status & STATUS_DESCRIPTOR_CLOSED) ? EWF_CLOSED : EWF_NONE;
    watch->flags |= (watch->event.events & EPOLLIN) ? EWF_WAITINGREAD : EWF_NONE;
    watch->flags |= (watch->event.events & EPOLLOUT) ? EWF_WAITINGWRITE : EWF_NONE;
    watch->flags |= (watch->event.events & EPOLLET) ? EWF_EDGETRIGGER : EWF_NONE;
    watch->flags |= (watch->event.events & EPOLLONESHOT) ? EWF_ONESHOT : EWF_NONE;

    /* add back in our lazyFlags that we dont check separately */
    watch->flags |= lazyFlags;

    /* update changed status for edgetrigger mode */
    if((oldFlags & EWF_READABLE) != (watch->flags & EWF_READABLE)) {
        watch->flags |= EWF_READCHANGED;
    }
    if((oldFlags & EWF_WRITEABLE) != (watch->flags & EWF_WRITEABLE)) {
        watch->flags |= EWF_WRITECHANGED;
    }
}

static gboolean _epollwatch_isReady(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    /* if its closed, not active, or no parent is watching it, then we are not ready */
    if((watch->flags & EWF_CLOSED) || !(watch->flags & EWF_ACTIVE) || !(watch->flags & EWF_WATCHING)) {
        return FALSE;
    }

    gboolean isReady = FALSE;

    gboolean hasReadEvent = (watch->flags & EWF_READABLE) && (watch->flags & EWF_WAITINGREAD) ? TRUE : FALSE;
    gboolean hasWriteEvent = (watch->flags & EWF_WRITEABLE) && (watch->flags & EWF_WAITINGWRITE) ? TRUE : FALSE;

    /* figure out if we should report an event */
    if(watch->flags & EWF_EDGETRIGGER) {
        /* edge-triggered mode is only ready if the read/write event status changed, unless there is
         * an event and we have yet to report it. */
        if(hasReadEvent && ((watch->flags & EWF_READCHANGED) || !(watch->flags & EWF_EDGETRIGGER_REPORTED))) {
            isReady = TRUE;
        }
        if(hasWriteEvent && ((watch->flags & EWF_WRITECHANGED) || !(watch->flags & EWF_EDGETRIGGER_REPORTED))) {
            isReady = TRUE;
        }
    } else {
        /* default level-triggered mode always reports events that exist */
        if(hasReadEvent || hasWriteEvent) {
            isReady =  TRUE;
        }
    }

    /* ONESHOT mode only reports once until a change happens */
    if(isReady && (watch->flags & EWF_ONESHOT) && (watch->flags & EWF_ONESHOT_REPORTED)) {
        isReady = FALSE;
    }

    return isReady;
}

static const gchar* _epoll_operationToStr(gint op) {
    switch(op) {
    case EPOLL_CTL_ADD:
        return "EPOLL_CTL_ADD";
    case EPOLL_CTL_DEL:
        return "EPOLL_CTL_DEL";
    case EPOLL_CTL_MOD:
        return "EPOLL_CTL_MOD";
    default:
        return "unknown";
    }
}

gint epoll_control(Epoll* epoll, gint operation, LegacyDescriptor* descriptor,
                   const struct epoll_event* event) {
    MAGIC_ASSERT(epoll);

    debug("epoll descriptor %i, operation %s, descriptor %i",
            epoll->super.handle, _epoll_operationToStr(operation), descriptor->handle);

    gint* watchHandleRef = descriptor_getHandleReference(descriptor);

    EpollWatch* watch = g_hash_table_lookup(epoll->watching, watchHandleRef);

    switch (operation) {
        case EPOLL_CTL_ADD: {
            /* EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor
             * fd is already registered with this epoll instance. */
            if(watch) {
                return -EEXIST;
            }

            /* start watching for status changes */
            watch = _epollwatch_new(epoll, descriptor, event);
            watch->flags |= EWF_WATCHING;
            g_hash_table_replace(epoll->watching, watchHandleRef, watch);

            /* It's added, so we need to listen for changes. Here we listen for
             * all statuses, because epoll will filter what it needs.
             * TODO: lean more heavily on statuslistener and simplify epoll.
             */
            statuslistener_setMonitorStatus(watch->listener,
                                            STATUS_DESCRIPTOR_ACTIVE | STATUS_DESCRIPTOR_CLOSED |
                                                STATUS_DESCRIPTOR_READABLE |
                                                STATUS_DESCRIPTOR_WRITABLE,
                                            SLF_ALWAYS);
            descriptor_addListener(watch->descriptor, watch->listener);

            /* initiate a callback if the new watched descriptor is ready */
            _epoll_descriptorStatusChanged(epoll, descriptor);

            break;
        }

        case EPOLL_CTL_MOD: {
            /* ENOENT op was EPOLL_CTL_MOD, and fd is not registered with this epoll instance */
            if(!watch) {
                return -ENOENT;
            }

            MAGIC_ASSERT(watch);
            utility_assert(event && (watch->flags & EWF_WATCHING));

            /* the user set new events */
            watch->event = *event;
            /* we would need to report the new event again if in ET or ONESHOT modes */
            watch->flags &= ~EWF_EDGETRIGGER_REPORTED;
            watch->flags &= ~EWF_ONESHOT_REPORTED;

            /* initiate a callback if the new event type on the watched descriptor is ready */
            _epoll_descriptorStatusChanged(epoll, descriptor);

            break;
        }

        case EPOLL_CTL_DEL: {
            /* ENOENT op was EPOLL_CTL_DEL, and fd is not registered with this epoll instance */
            if(!watch) {
                return -ENOENT;
            }

            MAGIC_ASSERT(watch);
            watch->flags &= ~EWF_WATCHING;

            /* its deleted, so stop listening for updates */
            statuslistener_setMonitorStatus(watch->listener, STATUS_NONE, SLF_NEVER);
            descriptor_removeListener(watch->descriptor, watch->listener);

            /* unref gets called on the watch when it is removed from these tables */
            g_hash_table_remove(epoll->ready, watchHandleRef);
            g_hash_table_remove(epoll->watching, watchHandleRef);

            break;
        }

        default: {
            warning("ignoring unrecognized operation");
            break;
        }
    }

    return 0;
}

guint epoll_getNumReadyEvents(Epoll* epoll) {
    MAGIC_ASSERT(epoll);
    return g_hash_table_size(epoll->ready);
}

gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray, gint eventArrayLength, gint* nEvents) {
    MAGIC_ASSERT(epoll);
    utility_assert(nEvents);

    /* return the available events in the eventArray, making sure not to
     * overflow. the number of actual events is returned in nEvents. */
    gint eventIndex = 0;

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, epoll->ready);
    while(g_hash_table_iter_next(&iter, &key, &value) && (eventIndex < eventArrayLength)) {
        EpollWatch* watch = value;
        MAGIC_ASSERT(watch);

        if(_epollwatch_isReady(watch)) {
            /* report the event */
            eventArray[eventIndex] = watch->event;
            eventArray[eventIndex].events = 0;

            if((watch->flags & EWF_READABLE) && (watch->flags & EWF_WAITINGREAD)) {
                eventArray[eventIndex].events |= EPOLLIN;
            }
            if((watch->flags & EWF_WRITEABLE) && (watch->flags & EWF_WAITINGWRITE)) {
                eventArray[eventIndex].events |= EPOLLOUT;
            }
            if(watch->flags & EWF_EDGETRIGGER) {
                eventArray[eventIndex].events |= EPOLLET;
            }

            /* event was just collected, unset the change status */
            watch->flags &= ~EWF_READCHANGED;
            watch->flags &= ~EWF_WRITECHANGED;

            eventIndex++;
            utility_assert(eventIndex <= eventArrayLength);

            if(watch->flags & EWF_EDGETRIGGER) {
                /* tag that an event was collected in ET mode */
                watch->flags |= EWF_EDGETRIGGER_REPORTED;
            }
            if(watch->flags & EWF_ONESHOT) {
                /* they collected the event, dont report any more */
                watch->flags |= EWF_ONESHOT_REPORTED;
            }
        }
    }

    *nEvents = eventIndex;

    debug("epoll descriptor %i collected %i events", descriptor_getHandle(&epoll->super), eventIndex);

    /* if we consumed all the events that we had to report,
     * then our parent descriptor can no longer read child epolls */
    descriptor_adjustStatus(
        &(epoll->super), STATUS_DESCRIPTOR_READABLE, epoll_getNumReadyEvents(epoll) ? TRUE : FALSE);

    return 0;
}

static void _epoll_descriptorStatusChanged(Epoll* epoll,
                                           LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(epoll);

    /* make sure we are actually watching the descriptor */
    gint* watchHandleRef = descriptor_getHandleReference(descriptor);
    EpollWatch* watch = g_hash_table_lookup(epoll->watching, watchHandleRef);

    /* if we are not watching, its an error because we shouldn't be listening */
    utility_assert(watch && (watch->descriptor == descriptor));

    debug("status changed in epoll %i for descriptor %i", descriptor_getHandle(&epoll->super), descriptor->handle);

    /* update the status for the child watch fd */
    _epollwatch_updateStatus(watch);

    /* check if its ready (has an event to report) now */
    if(_epollwatch_isReady(watch)) {
        if(!g_hash_table_contains(epoll->ready, watchHandleRef)) {
            _epollwatch_ref(watch);
            g_hash_table_replace(epoll->ready, watchHandleRef, watch);
        }
    } else {
        /* this calls unref on the watch if its in the table */
        g_hash_table_remove(epoll->ready, watchHandleRef);
    }

    /* check the status on the parent epoll fd and adjust as needed */
    descriptor_adjustStatus(
        &(epoll->super), STATUS_DESCRIPTOR_READABLE, epoll_getNumReadyEvents(epoll) ? TRUE : FALSE);
}
