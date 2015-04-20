/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <unistd.h>
#include "shadow.h"

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
    /* set if this watch exists in the reportable queue */
    EWF_REPORTING = 1 << 8,
    /* true if this watch is currently valid and in the watches table. this allows
     * support of lazy deletion of watches that are in the reportable queue when
     * we want to delete them, to avoid the O(n) removal time of the queue. */
    EWF_WATCHING = 1 << 9,
    /* set if edge-triggered events are enabled on the underlying shadow descriptor */
    EWF_EDGETRIGGER = 1 << 10,
    /* set if one-shot events are enabled on the underlying shadow descriptor */
    EWF_ONESHOT = 1 << 11,
};

typedef struct _EpollWatch EpollWatch;
struct _EpollWatch {
    /* the shadow descriptor we are watching for events */
    Descriptor* descriptor;
    /* the listener that will notify us when the descriptor status changes */
    Listener* listener;
    /* holds the actual event info */
    struct epoll_event event;
    /* current status of the underlying shadow descriptor */
    EpollWatchFlags flags;
    MAGIC_DECLARE;
};

typedef enum _EpollFlags EpollFlags;
enum _EpollFlags {
    EF_NONE = 0,
    /* a callback is currently scheduled to notify user
     * (used to avoid duplicate notifications) */
    EF_SCHEDULED = 1 << 0,
    /* the plugin closed the epoll descriptor, we should close as
     * soon as the notify is no longer scheduled */
    EF_CLOSED = 1 << 1,
};

struct _Epoll {
    /* epoll itself is also a descriptor */
    Descriptor super;

    /* other members specific to epoll */
    EpollFlags flags;

    /* holds the wrappers for the descriptors we are watching for events */
    GHashTable* watching;
    /* holds the watches which have events we need to report to the user */
    GQueue* reporting;

    SimulationTime lastWaitTime;
    Thread* ownerThread;
    gint osEpollDescriptor;

    MAGIC_DECLARE;
};

static EpollWatch* _epollwatch_new(Epoll* epoll, Descriptor* descriptor, struct epoll_event* event) {
    EpollWatch* watch = g_new0(EpollWatch, 1);
    MAGIC_INIT(watch);
    utility_assert(event);

    /* ref it for the EpollWatch, which also covers the listener reference
     * (which is freed below in _epollwatch_free) */
    descriptor_ref(descriptor);

    watch->listener = listener_new((CallbackFunc)epoll_descriptorStatusChanged, epoll, descriptor);
    watch->descriptor = descriptor;
    watch->event = *event;

    descriptor_addStatusListener(watch->descriptor, watch->listener);

    return watch;
}

static void _epollwatch_free(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    descriptor_removeStatusListener(watch->descriptor, watch->listener);
    listener_free(watch->listener);
    descriptor_unref(watch->descriptor);

    MAGIC_CLEAR(watch);
    g_free(watch);
}

/* should only be called from descriptor dereferencing the functionTable */
static void _epoll_free(Epoll* epoll) {
    MAGIC_ASSERT(epoll);

    /* this will go through all epollwatch items and remove the listeners
     * only from those that dont already exist in the watching table */
    while(!g_queue_is_empty(epoll->reporting)) {
        EpollWatch* watch = g_queue_pop_head(epoll->reporting);
        MAGIC_ASSERT(watch);
        /* turn off reporting bit */
        watch->flags &= ~EWF_REPORTING;
        if(!(watch->flags & EWF_WATCHING)) {
            _epollwatch_free(watch);
        }
    }
    g_queue_free(epoll->reporting);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, epoll->watching);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        _epollwatch_free((EpollWatch*) value);
    }
    g_hash_table_destroy(epoll->watching);

    close(epoll->osEpollDescriptor);

    utility_assert(epoll->ownerThread);
    thread_unref(epoll->ownerThread);

    MAGIC_CLEAR(epoll);
    g_free(epoll);
}

static void _epoll_close(Epoll* epoll) {
    MAGIC_ASSERT(epoll);

    /* mark the descriptor as closed */
    epoll->flags |= EF_CLOSED;

    /* only close it if there is no pending epoll notify event */
    gboolean isScheduled = (epoll->flags & EF_SCHEDULED) ? TRUE : FALSE;
    if(!isScheduled) {
        host_closeDescriptor(worker_getCurrentHost(), epoll->super.handle);
    }
}

DescriptorFunctionTable epollFunctions = {
    (DescriptorFunc) _epoll_close,
    (DescriptorFunc) _epoll_free,
    MAGIC_VALUE
};

Epoll* epoll_new(gint handle) {
    Epoll* epoll = g_new0(Epoll, 1);
    MAGIC_INIT(epoll);

    descriptor_init(&(epoll->super), DT_EPOLL, &epollFunctions, handle);

    /* allocate backend needed for managing events for this descriptor */
    epoll->watching = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);
    epoll->reporting = g_queue_new();

    /* the application may want us to watch some system files, so we need a
     * real OS epoll fd so we can offload that task.
     */
    epoll->osEpollDescriptor = epoll_create(1000);
    if(epoll->osEpollDescriptor == -1) {
        warning("error in epoll_create for OS events, errno=%i", errno);
    }

    /* keep track of which virtual application we need to notify of events
    epoll_new should be called as a result of an application syscall */
    epoll->ownerThread = worker_getActiveThread();
    utility_assert(epoll->ownerThread);
    thread_ref(epoll->ownerThread);

    /* the epoll descriptor itself is always able to be epolled */
    descriptor_adjustStatus(&(epoll->super), DS_ACTIVE, TRUE);

    return epoll;
}

static void _epollwatch_updateStatus(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    /* store the old flags that are only lazily updated */
    EpollWatchFlags lazyFlags = 0;
    lazyFlags |= (watch->flags & EWF_READCHANGED) ? EWF_READCHANGED : EWF_NONE;
    lazyFlags |= (watch->flags & EWF_WRITECHANGED) ? EWF_WRITECHANGED : EWF_NONE;
    lazyFlags |= (watch->flags & EWF_WATCHING) ? EWF_WATCHING : EWF_NONE;
    lazyFlags |= (watch->flags & EWF_REPORTING) ? EWF_REPORTING : EWF_NONE;

    /* reset our flags */
    EpollWatchFlags oldFlags = watch->flags;
    watch->flags = 0;

    /* check shadow descriptor status */
    DescriptorStatus status = descriptor_getStatus(watch->descriptor);
    watch->flags |= (status & DS_ACTIVE) ? EWF_ACTIVE : EWF_NONE;
    watch->flags |= (status & DS_READABLE) ? EWF_READABLE : EWF_NONE;
    watch->flags |= (status & DS_WRITABLE) ? EWF_WRITEABLE : EWF_NONE;
    watch->flags |= (status & DS_CLOSED) ? EWF_CLOSED : EWF_NONE;
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

static gboolean _epollwatch_needsNotify(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    if((watch->flags & EWF_CLOSED) || !(watch->flags & EWF_ACTIVE)) {
        return FALSE;
    }

    if(watch->flags & EWF_EDGETRIGGER) {
        if(((watch->flags & EWF_READABLE) &&
                (watch->flags & EWF_WAITINGREAD) && (watch->flags & EWF_READCHANGED)) ||
                ((watch->flags & EWF_WRITEABLE) &&
                (watch->flags & EWF_WAITINGWRITE) && (watch->flags & EWF_WRITECHANGED))) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        if(((watch->flags & EWF_READABLE) && (watch->flags & EWF_WAITINGREAD)) ||
                ((watch->flags & EWF_WRITEABLE) && (watch->flags & EWF_WAITINGWRITE))) {
            return TRUE;
        } else {
            return FALSE;
        }
    }
}

static void _epoll_trySchedule(Epoll* epoll) {
    /* schedule a shadow notification event if we have events to report */
    if(!(epoll->flags & EF_CLOSED) && !g_queue_is_empty(epoll->reporting)) {
        /* avoid duplicating events in the shadow event queue for our epoll */
        gboolean isScheduled = (epoll->flags & EF_SCHEDULED) ? TRUE : FALSE;
        if(!isScheduled && process_isRunning(thread_getParentProcess(epoll->ownerThread))) {
            /* schedule a notification event for our node */
            NotifyPluginEvent* event = notifyplugin_new(epoll->super.handle);
            SimulationTime delay = 1;
            worker_scheduleEvent((Event*)event, delay, 0);

            epoll->flags |= EF_SCHEDULED;
        }
    }
}

static void _epoll_check(Epoll* epoll, EpollWatch* watch) {
    MAGIC_ASSERT(epoll);
    MAGIC_ASSERT(watch);

    /* check status to see if we need to schedule a notification */
    _epollwatch_updateStatus(watch);
    gboolean needsNotify = _epollwatch_needsNotify(watch);

    if(needsNotify) {
        /* we need to report an event to user */
        if(!(watch->flags & EWF_REPORTING)) {
            /* check if parent epoll can read our child epoll event */
            gboolean setReadable = g_queue_is_empty(epoll->reporting);

            g_queue_push_tail(epoll->reporting, watch);
            watch->flags |= EWF_REPORTING;

            if(setReadable) {
                descriptor_adjustStatus(&(epoll->super), DS_READABLE, TRUE);
            }
        }
    } else {
        /* this watch no longer needs reporting
         * NOTE: the removal is also done lazily, so this else block
         * is technically not needed. its up for debate if its faster to
         * iterate the queue to remove the event now, versus swapping in
         * the node to collect events and lazily removing this later. */
        if(watch->flags & EWF_REPORTING) {
            g_queue_remove(epoll->reporting, watch);
            watch->flags &= ~EWF_REPORTING;

            /* check if parent epoll can still read child epoll events */
            if(g_queue_is_empty(epoll->reporting)) {
                descriptor_adjustStatus(&(epoll->super), DS_READABLE, FALSE);
            }
        }

        if((watch->flags & EWF_CLOSED) ||
                (!(watch->flags & EWF_WATCHING) && !(watch->flags & EWF_REPORTING))) {
            _epollwatch_free(watch);
        }
    }

    _epoll_trySchedule(epoll);
}

gint epoll_control(Epoll* epoll, gint operation, Descriptor* descriptor,
        struct epoll_event* event) {
    MAGIC_ASSERT(epoll);

    debug("epoll descriptor %i, operation %i, descriptor %i",
            epoll->super.handle, operation, descriptor->handle);

    EpollWatch* watch = g_hash_table_lookup(epoll->watching,
                        descriptor_getHandleReference(descriptor));

    switch (operation) {
        case EPOLL_CTL_ADD: {
            /* EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor
             * fd is already registered with this epoll instance. */
            if(watch) {
                return EEXIST;
            }

            /* start watching for status changes */
            watch = _epollwatch_new(epoll, descriptor, event);
            g_hash_table_replace(epoll->watching,
                    descriptor_getHandleReference(descriptor), watch);
            watch->flags |= EWF_WATCHING;

            /* initiate a callback if the new watched descriptor is ready */
            _epoll_check(epoll, watch);

            break;
        }

        case EPOLL_CTL_MOD: {
            /* ENOENT op was EPOLL_CTL_MOD, and fd is not registered with this epoll instance */
            if(!watch) {
                return ENOENT;
            }

            MAGIC_ASSERT(watch);
            utility_assert(event && (watch->flags & EWF_WATCHING));

            /* the user set new events */
            watch->event = *event;

            /* initiate a callback if the new event type on the watched descriptor is ready */
            _epoll_check(epoll, watch);

            break;
        }

        case EPOLL_CTL_DEL: {
            /* ENOENT op was EPOLL_CTL_DEL, and fd is not registered with this epoll instance */
            if(!watch) {
                return ENOENT;
            }

            MAGIC_ASSERT(watch);
            g_hash_table_remove(epoll->watching,
                    descriptor_getHandleReference(descriptor));
            watch->flags &= ~EWF_WATCHING;

            /* we can only delete it if its not in the reporting queue */
            if(!(watch->flags & EWF_REPORTING)) {
                _epollwatch_free(watch);
            } else {
                descriptor_removeStatusListener(watch->descriptor, watch->listener);
            }

            break;
        }

        default: {
            warning("ignoring unrecognized operation");
            break;
        }
    }

    return 0;
}

gint epoll_controlOS(Epoll* epoll, gint operation, gint fileDescriptor,
        struct epoll_event* event) {
    MAGIC_ASSERT(epoll);
    /* ask the OS about any events on our kernel epoll descriptor */
    return epoll_ctl(epoll->osEpollDescriptor, operation, fileDescriptor, event);
}

gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray,
        gint eventArrayLength, gint* nEvents) {
    MAGIC_ASSERT(epoll);
    utility_assert(nEvents);

    epoll->lastWaitTime = worker_getCurrentTime();

    /* return the available events in the eventArray, making sure not to
     * overflow. the number of actual events is returned in nEvents. */
    gint reportableLength = g_queue_get_length(epoll->reporting);
    gint eventArrayIndex = 0;

    for(gint i = 0; i < eventArrayLength && i < reportableLength; i++) {
        EpollWatch* watch = g_queue_pop_head(epoll->reporting);
        MAGIC_ASSERT(watch);

        /* lazy-delete items that we are no longer watching */
        if(!(watch->flags & EWF_WATCHING)) {
            watch->flags &= ~EWF_REPORTING;
            _epollwatch_free(watch);
            continue;
        }

        /* double check that we should still notify this event */
        _epollwatch_updateStatus(watch);
        if(_epollwatch_needsNotify(watch)) {
            /* report the event */
            eventArray[eventArrayIndex] = watch->event;
            eventArray[eventArrayIndex].events = 0;
            eventArray[eventArrayIndex].events |=
                    ((watch->flags & EWF_READABLE) && (watch->flags & EWF_WAITINGREAD)) ? EPOLLIN : 0;
            eventArray[eventArrayIndex].events |=
                    ((watch->flags & EWF_WRITEABLE) && (watch->flags & EWF_WAITINGWRITE)) ? EPOLLOUT : 0;
            eventArray[eventArrayIndex].events |= (watch->flags & EWF_EDGETRIGGER) ? EPOLLET : 0;
            eventArrayIndex++;
            utility_assert(eventArrayIndex <= eventArrayLength);

            if(watch->flags & EWF_ONESHOT) {
                /* they collected the event, dont report any more */
                watch->flags &= ~EWF_REPORTING;
            } else {
                /* this watch persists until the descriptor status changes */
                g_queue_push_tail(epoll->reporting, watch);
                utility_assert(watch->flags & EWF_REPORTING);
            }
        } else {
            watch->flags &= ~EWF_REPORTING;
        }
    }

    /* if we consumed all the events that we had to report,
     * then our parent descriptor can no longer read child epolls */
    if(reportableLength > 0 && g_queue_is_empty(epoll->reporting)) {
        descriptor_adjustStatus(&(epoll->super), DS_READABLE, FALSE);
    }

    gint space = eventArrayLength - eventArrayIndex;
    if(space) {
        /* now we have to get events from the OS descriptors */
        struct epoll_event osEvents[space];
        /* since we are in shadow context, this will be forwarded to the OS epoll */
        gint nos = epoll_wait(epoll->osEpollDescriptor, osEvents, space, 0);

        if(nos == -1) {
            warning("error in epoll_wait for OS events on epoll fd %i", epoll->osEpollDescriptor);
        }

        /* nos will fit into eventArray */
        for(gint j = 0; j < nos; j++) {
            eventArray[eventArrayIndex] = osEvents[j];
            eventArrayIndex++;
            utility_assert(eventArrayIndex <= eventArrayLength);
        }
    }

    *nEvents = eventArrayIndex;

    debug("epoll descriptor %i collected %i events", epoll->super.handle, eventArrayIndex);

    return 0;
}

void epoll_descriptorStatusChanged(Epoll* epoll, Descriptor* descriptor) {
    MAGIC_ASSERT(epoll);

    /* make sure we are actually watching the descriptor */
    EpollWatch* watch = g_hash_table_lookup(epoll->watching,
            descriptor_getHandleReference(descriptor));

    /* if we are not watching, its an error because we shouldn't be listening */
    utility_assert(watch && (watch->descriptor == descriptor));

    /* check the status and take the appropriate action */
    _epoll_check(epoll, watch);
}

void epoll_tryNotify(Epoll* epoll) {
    MAGIC_ASSERT(epoll);

    /* event is being executed from the scheduler, so its no longer scheduled */
    epoll->flags &= ~EF_SCHEDULED;

    /* if it was closed in the meantime, do the actual close now */
    gboolean isClosed = (epoll->flags & EF_CLOSED) ? TRUE : FALSE;
    if(isClosed || !thread_isRunning(epoll->ownerThread)) {
        host_closeDescriptor(worker_getCurrentHost(), epoll->super.handle);
        return;
    }

    /* make sure this doesn't get destroyed if closed while notifying */
    descriptor_ref(&epoll->super);

    /* we should notify the plugin only if we still have some events to report
     * XXX: what if our watches are empty, but the OS desc has events? */
    if(!g_queue_is_empty(epoll->reporting)) {
        /* notify application to collect the reportable events */
        process_notify(thread_getParentProcess(epoll->ownerThread), epoll->ownerThread);

        /* we just notified the application of the events, so reset the change status
         * and only report the event again if necessary */
        GQueue* newReporting = g_queue_new();
        while(!g_queue_is_empty(epoll->reporting)) {
            EpollWatch* watch = g_queue_pop_head(epoll->reporting);
            watch->flags &= ~EWF_READCHANGED;
            watch->flags &= ~EWF_WRITECHANGED;
            if(_epollwatch_needsNotify(watch)) {
                /* remains in the reporting queue */
                g_queue_push_tail(newReporting, watch);
            } else {
                /* no longer needs reporting */
                watch->flags &= ~EWF_REPORTING;
            }
        }
        g_queue_free(epoll->reporting);
        epoll->reporting = newReporting;
    }

    /* check if we need to be notified again */
    _epoll_trySchedule(epoll);

    descriptor_unref(&epoll->super);
}
