/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/host/descriptor/epoll.h"

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <string.h>
#include <sys/epoll.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"
#include "main/utility/utility.h"

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

typedef enum _EpollWatchTypes EpollWatchTypes;
enum _EpollWatchTypes {
    EWT_LEGACY_FILE,
    EWT_GENERIC_FILE,
};

typedef union _EpollWatchObject EpollWatchObject;
union _EpollWatchObject {
    LegacyFile* as_legacy_file;
    const File* as_file;
};

typedef struct _EpollWatch EpollWatch;
struct _EpollWatch {
    /* A unique id for this watch relative to other watches in this epoll instance.
     * This id encodes a total ordering of watches so they can be deterministically sorted. */
    uint64_t id;
    /* the type of object we are watching (for example descriptor or file) */
    EpollWatchTypes watchType;
    /* the object we are watching for events */
    EpollWatchObject watchObject;
    /* the fd of the object we are watching */
    int fd;
    /* the listener that notifies us when status changes */
    StatusListener* listener;
    /* holds the actual event info */
    struct epoll_event event;
    /* current status of the underlying shadow descriptor */
    EpollWatchFlags flags;
    /* The last time we reported an event on this watch.
     * This is used to ensure fairness across watches when reporting events. */
    CEmulatedTime last_reported_event_time;
    gint referenceCount;
    MAGIC_DECLARE;
};

/* the epoll tables are indexed by the (fd, objectPtr) tuple so you can add the same object
 * multiple times under different fds, and you can add the same fd multiple times as long as the
 * object is different */
typedef struct _EpollKey EpollKey;
struct _EpollKey {
    int fd;
    /* store the pointer as an int so that we never accidentally de-reference it */
    uintptr_t objectPtr;
};

struct _Epoll {
    /* epoll itself is also a descriptor */
    LegacyFile super;

    /* holds the wrappers for the descriptors we are watching for events */
    GHashTable* watching;

    /* holds the descriptors that we are watching that have events */
    GHashTable* ready;

    /* A counter for sorting watches, for guaranteeing determinism when reporting events. */
    uint64_t watch_id_counter;

    MAGIC_DECLARE;
};

static EpollKey* _epollkey_new(int fd, uintptr_t objectPtr) {
    EpollKey* key = g_new0(EpollKey, 1);
    utility_debugAssert(key);

    key->fd = fd;
    key->objectPtr = objectPtr;

    return key;
}

static guint _epollkey_hash(gconstpointer ptr) {
    const EpollKey* key = ptr;
    return g_int_hash(&key->fd) ^ g_int_hash(&key->objectPtr);
}

static gboolean _epollkey_equal(gconstpointer ptr_1, gconstpointer ptr_2) {
    const EpollKey* key_1 = ptr_1;
    const EpollKey* key_2 = ptr_2;
    return key_1->fd == key_2->fd && key_1->objectPtr == key_2->objectPtr;
}

/* compare by the associated file descriptor */
static gint _epollkey_compare(gconstpointer ptr_1, gconstpointer ptr_2) {
    const EpollKey* key_1 = ptr_1;
    const EpollKey* key_2 = ptr_2;

    /* if the fds are the same but the objects are different, something went wrong */
    if (key_1->fd == key_2->fd) {
        assert(key_1->objectPtr == key_2->objectPtr);
    }

    return key_1->fd - key_2->fd;
}

static gint _epollwatch_compare(gconstpointer ptr_1, gconstpointer ptr_2) {
    const EpollWatch* watch_1 = ptr_1;
    const EpollWatch* watch_2 = ptr_2;
    /* Prioritize watches whose last events were reported longest ago. */
    if (watch_1->last_reported_event_time < watch_2->last_reported_event_time) {
        /* watch_1 was reported longest ago and should come first. */
        return -1;
    } else if (watch_1->last_reported_event_time > watch_2->last_reported_event_time) {
        /* watch_2 was reported longest ago and should come first. */
        return 1;
    } else {
        /* Both were previously reported at the same time (or never reported yet),
         * so now we fall back to the deterministic unique id ordering. */
        utility_debugAssert(watch_1->id != watch_2->id);
        return (watch_1->id < watch_2->id) ? -1 : 1;
    }
}

/* forward declaration */
static void _epoll_fileStatusChanged(Epoll* epoll, const EpollKey* key);

// Will take its own reference to the file object.
static EpollWatch* _epollwatch_new(Epoll* epoll, int fd, EpollWatchTypes type,
                                   EpollWatchObject object, const struct epoll_event* event,
                                   const Host* host) {
    EpollWatch* watch = g_new0(EpollWatch, 1);
    MAGIC_INIT(watch);
    utility_debugAssert(event);
    utility_debugAssert(epoll);

    /* ref it for the EpollWatch, which also covers the listener reference
     * (which is freed below in _epollwatch_free) */
    if (type == EWT_LEGACY_FILE) {
        legacyfile_ref(object.as_legacy_file);
    } else if (type == EWT_GENERIC_FILE) {
        object.as_file = file_cloneRef(object.as_file);
    }

    watch->id = ++epoll->watch_id_counter;
    watch->watchType = type;
    watch->watchObject = object;
    watch->fd = fd;
    watch->event = *event;
    watch->referenceCount = 1;

    uintptr_t objectPtr;
    if (watch->watchType == EWT_LEGACY_FILE) {
        objectPtr = (uintptr_t)(void*)object.as_legacy_file;
    } else if (watch->watchType == EWT_GENERIC_FILE) {
        objectPtr = file_getCanonicalHandle(object.as_file);
    } else {
        warning("Unrecognized epoll watch type: %d", type);
        objectPtr = (uintptr_t)NULL;
    }

    EpollKey *key = _epollkey_new(fd, objectPtr);

    /* Create the listener and ref the objects held by the listener.
     * The watch object already holds a ref to the descriptor so we
     * don't ref it again. */
    watch->listener = statuslistener_new(
        (StatusCallbackFunc)_epoll_fileStatusChanged, epoll, NULL, key, g_free, host);

    worker_count_allocation(EpollWatch);
    return watch;
}

static void _epollwatch_free(EpollWatch* watch) {
    MAGIC_ASSERT(watch);

    if (watch->watchType == EWT_LEGACY_FILE) {
        legacyfile_removeListener(watch->watchObject.as_legacy_file, watch->listener);
        legacyfile_unref(watch->watchObject.as_legacy_file);
    } else if (watch->watchType == EWT_GENERIC_FILE) {
        file_removeListener(watch->watchObject.as_file, watch->listener);
        file_drop(watch->watchObject.as_file);
    }

    statuslistener_unref(watch->listener);

    worker_count_deallocation(EpollWatch);
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

static Epoll* _epoll_fromLegacyFile(LegacyFile* descriptor) {
    utility_debugAssert(legacyfile_getType(descriptor) == DT_EPOLL);
    return (Epoll*)descriptor;
}

/* should only be called from descriptor dereferencing the functionTable */
static void _epoll_free(LegacyFile* descriptor) {
    Epoll* epoll = _epoll_fromLegacyFile(descriptor);
    MAGIC_ASSERT(epoll);

    /* this unrefs all of the remaining watches */
    g_hash_table_destroy(epoll->watching);
    g_hash_table_destroy(epoll->ready);

    legacyfile_clear((LegacyFile*)epoll);
    MAGIC_CLEAR(epoll);
    g_free(epoll);

    worker_count_deallocation(Epoll);
}

void epoll_clearWatchListeners(Epoll* epoll) {
    MAGIC_ASSERT(epoll);

    /* Iterate the hash table in a deterministic order.
     * It might be better to maintain the watching values in a sorted structure so we
     * don't have to re-sort every time this function is called. One option is:
     * https://developer.gnome.org/glib/2.68/glib-Balanced-Binary-Trees.html
     * We should probably check the performance before/after such a change. */
    GList* watch_list = g_hash_table_get_values(epoll->watching);
    GList* next_item = NULL;

    /* Prepare the list for deterministic iteration. */
    if (watch_list != NULL) {
        watch_list = g_list_sort(watch_list, _epollwatch_compare);
        next_item = g_list_first(watch_list);
    }

    /* make sure none of our watch descriptors notify us anymore */
    while (next_item != NULL) {
        EpollWatch* watch = next_item->data;
        MAGIC_ASSERT(watch);

        statuslistener_setMonitorStatus(watch->listener, STATUS_NONE, SLF_NEVER);

        if (watch->watchType == EWT_LEGACY_FILE) {
            legacyfile_removeListener(watch->watchObject.as_legacy_file, watch->listener);
        } else if (watch->watchType == EWT_GENERIC_FILE) {
            file_removeListener(watch->watchObject.as_file, watch->listener);
        }

        next_item = g_list_next(next_item);
    }

    /* Cleanup just the list but not the list values, which are owned by the hash table. */
    if (watch_list) {
        g_list_free(watch_list);
    }
}

void epoll_reset(Epoll* epoll) {
    MAGIC_ASSERT(epoll);
    epoll_clearWatchListeners(epoll);
    // Removing will also unref previously stored descriptors
    g_hash_table_remove_all(epoll->ready);
    g_hash_table_remove_all(epoll->watching);
}

static void _epoll_close(LegacyFile* descriptor, const Host* host) {
    Epoll* epoll = _epoll_fromLegacyFile(descriptor);
    MAGIC_ASSERT(epoll);
    epoll_clearWatchListeners(epoll);
}

LegacyFileFunctionTable epollFunctions = {_epoll_close, NULL, _epoll_free, MAGIC_VALUE};

Epoll* epoll_new() {
    Epoll* epoll = g_new0(Epoll, 1);
    MAGIC_INIT(epoll);

    legacyfile_init(&(epoll->super), DT_EPOLL, &epollFunctions);

    /* allocate backend needed for managing events for this descriptor */
    epoll->watching = g_hash_table_new_full(_epollkey_hash, _epollkey_equal, g_free, (GDestroyNotify)_epollwatch_unref);
    epoll->ready = g_hash_table_new_full(_epollkey_hash, _epollkey_equal, g_free, (GDestroyNotify)_epollwatch_unref);

    /* the epoll descriptor itself is always able to be epolled */
    legacyfile_adjustStatus(&(epoll->super), STATUS_FILE_ACTIVE, TRUE, 0);

    worker_count_allocation(Epoll);

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
    Status status;
    if (watch->watchType == EWT_LEGACY_FILE) {
        status = legacyfile_getStatus(watch->watchObject.as_legacy_file);
    } else if (watch->watchType == EWT_GENERIC_FILE) {
        status = file_getStatus(watch->watchObject.as_file);
    }

    watch->flags |= (status & STATUS_FILE_ACTIVE) ? EWF_ACTIVE : EWF_NONE;
    watch->flags |= (status & STATUS_FILE_READABLE) ? EWF_READABLE : EWF_NONE;
    watch->flags |= (status & STATUS_FILE_WRITABLE) ? EWF_WRITEABLE : EWF_NONE;
    watch->flags |= (status & STATUS_FILE_CLOSED) ? EWF_CLOSED : EWF_NONE;
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

// Increases the reference count of the inner file.
static void _getWatchObject(const Descriptor* descriptor, EpollWatchTypes* watchType,
                            EpollWatchObject* watchObject) {
    LegacyFile* legacyDescriptor = descriptor_asLegacyFile(descriptor);

    /* if the descriptor is for a legacy file */
    if (legacyDescriptor != NULL) {
        *watchType = EWT_LEGACY_FILE;
        legacyfile_ref(legacyDescriptor);
        watchObject->as_legacy_file = legacyDescriptor;
    } else {
        const File* file = descriptor_newRefFile(descriptor);
        /* if the descriptor is for a generic file object */
        if (file != NULL) {
            *watchType = EWT_GENERIC_FILE;
            watchObject->as_file = file;
        } else {
            utility_panic("unrecognized watch object");
        }
    }
}

gint epoll_control(Epoll* epoll, gint operation, int fd, const Descriptor* descriptor,
                   const struct epoll_event* event, const Host* host) {
    MAGIC_ASSERT(epoll);

    trace("epoll descriptor %p, operation %s, descriptor %i", &epoll->super,
          _epoll_operationToStr(operation), fd);

    EpollWatchTypes watchType;
    EpollWatchObject watchObject;

    /* get the watch type and object for the provided descriptor object */
    _getWatchObject(descriptor, &watchType, &watchObject);

    /* this on-stack key can be used for lookups only, not new entries */
    EpollKey key;
    key.fd = fd;
    switch (watchType) {
        case EWT_LEGACY_FILE:
            key.objectPtr = (uintptr_t)(void*)watchObject.as_legacy_file;
            break;
        case EWT_GENERIC_FILE:
            key.objectPtr = file_getCanonicalHandle(watchObject.as_file);
            break;
        default: utility_panic("unrecognized watch type");
    }

    EpollWatch* watch = g_hash_table_lookup(epoll->watching, &key);

    int rv = 0;

    switch (operation) {
        case EPOLL_CTL_ADD: {
            /* Check if we're trying to add a file that's already been closed.
             * Typically a file that is referenced in the descriptor table
             * should never be a closed file, but Shadow's TCP sockets do close
             * themselves even if there are still file handles (see
             * `_tcp_endOfFileSignalled`), so we need to check this. */
            Status status;
            if (watchType == EWT_LEGACY_FILE) {
                status = legacyfile_getStatus(watchObject.as_legacy_file);
            } else if (watchType == EWT_GENERIC_FILE) {
                status = file_getStatus(watchObject.as_file);
            }
            if (status & STATUS_FILE_CLOSED) {
                warning("Attempted to add a closed file to epoll %p", epoll);
                rv = -EBADF;
                break;
            }

            /* EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor
             * fd is already registered with this epoll instance. */
            if(watch) {
                rv = -EEXIST;
                break;
            }

            /* start watching for status changes */
            watch = _epollwatch_new(epoll, fd, watchType, watchObject, event, host);
            watch->flags |= EWF_WATCHING;
            gpointer new_key = _epollkey_new(key.fd, key.objectPtr);
            g_hash_table_replace(epoll->watching, new_key, watch);

            /* It's added, so we need to listen for changes. Here we listen for
             * all statuses, because epoll will filter what it needs.
             * TODO: lean more heavily on statuslistener and simplify epoll.
             */
            statuslistener_setMonitorStatus(watch->listener,
                                            STATUS_FILE_ACTIVE | STATUS_FILE_CLOSED |
                                                STATUS_FILE_READABLE | STATUS_FILE_WRITABLE,
                                            SLF_ALWAYS);
            if (watch->watchType == EWT_LEGACY_FILE) {
                legacyfile_addListener(watch->watchObject.as_legacy_file, watch->listener);
            } else if (watch->watchType == EWT_GENERIC_FILE) {
                file_addListener(watch->watchObject.as_file, watch->listener);
            }

            /* initiate a callback if the new watched object is ready */
            _epoll_fileStatusChanged(epoll, &key);

            break;
        }

        case EPOLL_CTL_MOD: {
            /* ENOENT op was EPOLL_CTL_MOD, and fd is not registered with this epoll instance */
            if(!watch) {
                rv = -ENOENT;
                break;
            }

            MAGIC_ASSERT(watch);
            utility_debugAssert(event && (watch->flags & EWF_WATCHING));

            /* the user set new events */
            watch->event = *event;
            /* we would need to report the new event again if in ET or ONESHOT modes */
            watch->flags &= ~EWF_EDGETRIGGER_REPORTED;
            watch->flags &= ~EWF_ONESHOT_REPORTED;

            /* initiate a callback if the new event type on the watched object is ready */
            _epoll_fileStatusChanged(epoll, &key);

            break;
        }

        case EPOLL_CTL_DEL: {
            /* ENOENT op was EPOLL_CTL_DEL, and fd is not registered with this epoll instance */
            if(!watch) {
                rv = -ENOENT;
                break;
            }

            MAGIC_ASSERT(watch);
            watch->flags &= ~EWF_WATCHING;

            /* its deleted, so stop listening for updates */
            statuslistener_setMonitorStatus(watch->listener, STATUS_NONE, SLF_NEVER);
            if (watch->watchType == EWT_LEGACY_FILE) {
                legacyfile_removeListener(watch->watchObject.as_legacy_file, watch->listener);
            } else if (watch->watchType == EWT_GENERIC_FILE) {
                file_removeListener(watch->watchObject.as_file, watch->listener);
            }

            /* unref gets called on the watch when it is removed from these tables */
            g_hash_table_remove(epoll->ready, &key);
            g_hash_table_remove(epoll->watching, &key);
            /* if that was the last watch, this epoll is not readable to its parents */
            _epoll_fileStatusChanged(epoll, NULL);

            break;
        }

        default: {
            warning("ignoring unrecognized operation");
            rv = -EINVAL;
            break;
        }
    }

    switch (watchType) {
        case EWT_LEGACY_FILE:
            legacyfile_unref(watchObject.as_legacy_file);
            break;
        case EWT_GENERIC_FILE:
            file_drop(watchObject.as_file);
            break;
        default: utility_panic("unrecognized watch type");
    }

    return rv;
}

guint epoll_getNumReadyEvents(Epoll* epoll) {
    MAGIC_ASSERT(epoll);
    return g_hash_table_size(epoll->ready);
}

gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray, gint eventArrayLength, gint* nEvents) {
    MAGIC_ASSERT(epoll);
    utility_debugAssert(nEvents);

    /* return the available events in the eventArray, making sure not to
     * overflow. the number of actual events is returned in nEvents. */
    gint eventIndex = 0;

    /*
     * We need to guarantee that the events are returned in a deterministic order when the
     * simulation is run multiple times, so we cannot use hash table iterator.
     * Using a list here has some potential performance implications:
     * - O(n) to loop the hash table and create the list of values
     * - O(n) to sort the list
     * - O(n) for our iteration of the list
     * We think that the ready list is typically small and so 3*O(n) will be small in practice.
     * If this turns out not to be the case, we could consider maintaining a sorted object of the
     * ready watch values alongside the epoll->ready hash table instead, which may allow us
     * to improve the performance of this function. One option is to use binary trees, e.g.,
     * https://developer.gnome.org/glib/2.68/glib-Balanced-Binary-Trees.html
     * We should test the performance before and after such a change.
     */
    GList* ready_list = g_hash_table_get_keys(epoll->ready);
    GList* next_key = NULL;

    /* Prepare the list for deterministic iteration. */
    if (ready_list != NULL) {
        ready_list = g_list_sort(ready_list, _epollkey_compare);
        next_key = g_list_first(ready_list);
    }

    GList* not_ready_list = NULL;

    /* Iterate the list. */
    while ((next_key != NULL) && (eventIndex < eventArrayLength)) {
        EpollKey* key = next_key->data;
        assert(key != NULL);

        EpollWatch* watch = g_hash_table_lookup(epoll->ready, key);
        MAGIC_ASSERT(watch);

        if (_epollwatch_isReady(watch)) {
            /* report the event */
            eventArray[eventIndex] = watch->event;
            eventArray[eventIndex].events = 0;

            if((watch->flags & EWF_READABLE) && (watch->flags & EWF_WAITINGREAD)) {
                eventArray[eventIndex].events |= EPOLLIN;
            }
            if((watch->flags & EWF_WRITEABLE) && (watch->flags & EWF_WAITINGWRITE)) {
                eventArray[eventIndex].events |= EPOLLOUT;
            }

            /* Record that we are reporting the event now. */
            watch->last_reported_event_time = worker_getCurrentEmulatedTime();

            /* event was just collected, unset the change status */
            watch->flags &= ~EWF_READCHANGED;
            watch->flags &= ~EWF_WRITECHANGED;

            eventIndex++;
            utility_debugAssert(eventIndex <= eventArrayLength);

            if(watch->flags & EWF_EDGETRIGGER) {
                /* tag that an event was collected in ET mode */
                watch->flags |= EWF_EDGETRIGGER_REPORTED;
            }
            if(watch->flags & EWF_ONESHOT) {
                /* they collected the event, dont report any more */
                watch->flags |= EWF_ONESHOT_REPORTED;
            }

            /* record any that are no longer ready */
            if (!_epollwatch_isReady(watch)) {
                not_ready_list = g_list_append(not_ready_list, key);
            }
        } else {
            error("epoll %p ready list has items that aren't ready", &epoll->super);
        }

        next_key = g_list_next(next_key);
    }

    /* Cleanup just the list but not the list values, which are owned by the hash table. */
    if (ready_list) {
        g_list_free(ready_list);
    }

    *nEvents = eventIndex;

    trace("epoll descriptor %p collected %i events", &epoll->super, eventIndex);

    next_key = NULL;
    if (not_ready_list) {
        next_key = g_list_first(not_ready_list);
    }

    /* We modified some watched objects above, so remove any that are no longer ready. */
    while (next_key != NULL) {
        EpollKey* key = next_key->data;
        gboolean removed = g_hash_table_remove(epoll->ready, key);
        assert(removed);

        next_key = g_list_next(next_key);
    }

    if (not_ready_list) {
        g_list_free(not_ready_list);
    }

    /* if we consumed all the events that we had to report,
     * then our parent descriptor can no longer read child epolls */
    legacyfile_adjustStatus(
        &(epoll->super), STATUS_FILE_READABLE, epoll_getNumReadyEvents(epoll) ? TRUE : FALSE, 0);

    return 0;
}

static void _epoll_fileStatusChanged(Epoll* epoll, const EpollKey* key) {
    MAGIC_ASSERT(epoll);

    trace("status changed on epoll %p", &epoll->super);

    if (key != NULL) {
        EpollWatch* watch = g_hash_table_lookup(epoll->watching, key);
        if (watch != NULL) {
            trace("status changed in epoll %p on watched descriptor %i", &epoll->super, watch->fd);

            /* update the status for the child watch fd */
            _epollwatch_updateStatus(watch);

            /* check if its ready (has an event to report) now */
            if (_epollwatch_isReady(watch)) {
                if (!g_hash_table_contains(epoll->ready, key)) {
                    _epollwatch_ref(watch);
                    gpointer keyCopy = _epollkey_new(key->fd, key->objectPtr);
                    g_hash_table_replace(epoll->ready, keyCopy, watch);
                }
            } else {
                /* this calls unref on the watch if its in the table */
                g_hash_table_remove(epoll->ready, key);
            }

            /* if it's closed, then remove it from the watching list */
            if (watch->flags & EWF_CLOSED) {
                /* unref gets called on the watch when it is removed from these tables */
                g_hash_table_remove(epoll->watching, key);
                /* we should have removed it from the ready list earlier */
                utility_debugAssert(!g_hash_table_contains(epoll->ready, key));
            }
        }
    }

    /* check the status on the parent epoll fd and adjust as needed */
    legacyfile_adjustStatus(
        &(epoll->super), STATUS_FILE_READABLE, epoll_getNumReadyEvents(epoll) ? TRUE : FALSE, 0);
}
