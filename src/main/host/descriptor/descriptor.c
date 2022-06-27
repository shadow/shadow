/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/descriptor.h"

#include <stddef.h>

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"
#include "main/utility/utility.h"

void legacydesc_init(LegacyDescriptor* descriptor, LegacyDescriptorType type,
                     DescriptorFunctionTable* funcTable) {
    utility_assert(descriptor && funcTable);

    MAGIC_INIT(descriptor);
    MAGIC_INIT(funcTable);
    descriptor->funcTable = funcTable;
    descriptor->type = type;
    descriptor->listeners = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)statuslistener_unref);
    descriptor->refCountStrong = 1;
    descriptor->refCountWeak = 0;

    trace("Descriptor %p has been initialized now", descriptor);

    worker_count_allocation(LegacyDescriptor);
}

void legacydesc_clear(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    if (descriptor->listeners) {
        g_hash_table_destroy(descriptor->listeners);
    }
    MAGIC_CLEAR(descriptor);
}

static void _legacydesc_cleanup(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    if (descriptor->funcTable->cleanup) {
        trace("Descriptor %p calling vtable cleanup now", descriptor);
        descriptor->funcTable->cleanup(descriptor);
    }
}

static void _legacydesc_free(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    trace("Descriptor %p calling vtable free now", descriptor);
    descriptor->funcTable->free(descriptor);

    worker_count_deallocation(LegacyDescriptor);
}

void legacydesc_ref(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    // should not increment the strong count when there are only weak references left
    utility_assert(descriptor->refCountStrong > 0);

    (descriptor->refCountStrong)++;
    trace("Descriptor %p strong_ref++ to %i (weak_ref=%i)", descriptor, descriptor->refCountStrong,
          descriptor->refCountWeak);
}

void legacydesc_unref(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountStrong)--;
    trace("Descriptor %p strong_ref-- to %i (weak_ref=%i)", descriptor, descriptor->refCountStrong,
          descriptor->refCountWeak);

    utility_assert(descriptor->refCountStrong >= 0);

    if (descriptor->refCountStrong > 0) {
        // there are strong references, so do nothing
        return;
    }

    if (descriptor->refCountWeak > 0) {
        // this was the last strong reference, but there are weak references, so cleanup only
        trace("Descriptor %p kept alive by weak count of %d", descriptor, descriptor->refCountWeak);

        // create a temporary weak reference to prevent the _legacydesc_cleanup() from calling
        // legacydesc_unrefWeak() and running the _legacydesc_free() while still running the
        // _legacydesc_cleanup()
        legacydesc_refWeak(descriptor);
        _legacydesc_cleanup(descriptor);
        legacydesc_unrefWeak(descriptor);

        return;
    }

    // this was the last strong reference and no weak references, so cleanup and free
    _legacydesc_cleanup(descriptor);
    _legacydesc_free(descriptor);
}

void legacydesc_refWeak(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountWeak)++;
    trace("Descriptor %p weak_ref++ to %i (strong_ref=%i)", descriptor, descriptor->refCountWeak,
          descriptor->refCountStrong);
}

void legacydesc_unrefWeak(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountWeak)--;
    trace("Descriptor %p weak_ref-- to %i (strong_ref=%i)", descriptor, descriptor->refCountWeak,
          descriptor->refCountStrong);

    utility_assert(descriptor->refCountWeak >= 0);

    if (descriptor->refCountStrong > 0 || descriptor->refCountWeak > 0) {
        // there are references (strong or weak), so do nothing
        return;
    }

    // this was the last weak reference and no strong references, so we should free
    // _legacydesc_cleanup() should have been called earlier when the strong count reached 0
    _legacydesc_free(descriptor);
}

void legacydesc_close(LegacyDescriptor* descriptor, Host* host) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    // if it's already closed, exit early
    if ((legacydesc_getStatus(descriptor) & STATUS_DESCRIPTOR_CLOSED) != 0) {
        warning("Attempting to close an already-closed descriptor");
        return;
    }

    trace("Descriptor %p calling vtable close now", descriptor);
    legacydesc_adjustStatus(descriptor, STATUS_DESCRIPTOR_CLOSED, TRUE);

    descriptor->funcTable->close(descriptor, host);
}

LegacyDescriptorType legacydesc_getType(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->type;
}

void legacydesc_setOwnerProcess(LegacyDescriptor* descriptor, Process* ownerProcess) {
    MAGIC_ASSERT(descriptor);
    descriptor->ownerProcess = ownerProcess;
}

Process* legacydesc_getOwnerProcess(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->ownerProcess;
}

#ifdef DEBUG
static gchar* _legacydesc_statusToString(Status ds) {
    GString* string = g_string_new(NULL);
    if (ds & STATUS_DESCRIPTOR_ACTIVE) {
        g_string_append_printf(string, "ACTIVE|");
    }
    if (ds & STATUS_DESCRIPTOR_READABLE) {
        g_string_append_printf(string, "READABLE|");
    }
    if (ds & STATUS_DESCRIPTOR_WRITABLE) {
        g_string_append_printf(string, "WRITEABLE|");
    }
    if (ds & STATUS_DESCRIPTOR_CLOSED) {
        g_string_append_printf(string, "CLOSED|");
    }
    if(string->len == 0) {
        g_string_append_printf(string, "NONE|");
    }
    g_string_truncate(string, string->len-1);
    return g_string_free(string, FALSE);
}
#endif

static void _legacydesc_handleStatusChange(LegacyDescriptor* descriptor, Status oldStatus) {
    MAGIC_ASSERT(descriptor);

    /* Identify which bits changed, if any. */
    Status statusesChanged = descriptor->status ^ oldStatus;

    if (!statusesChanged) {
        return;
    }

#ifdef DEBUG
    gchar* before = _legacydesc_statusToString(oldStatus);
    gchar* after = _legacydesc_statusToString(descriptor->status);
    trace("Status changed on desc %p, from %s to %s", descriptor, before, after);
    g_free(before);
    g_free(after);
#endif

    /* Tell our listeners there was some activity on this descriptor.
     * We can't use an iterator here, because the listener table may
     * be modified in the body of the while loop below, in the onStatusChanged
     * callback. Instead we get a list of the keys and do lookups on those.*/
    GList* listenerList = g_hash_table_get_keys(descriptor->listeners);

    // Iterate the listeners in deterministic order. It's probably better to
    // maintain the items in a sorted structure, e.g. a ring, to make it easier
    // or more efficient to iterate deterministically while also moving the ring
    // entry pointer so we don't always wake up the same listener first on every
    // iteration and possibly starve the others.
    GList* item = NULL;
    if (listenerList != NULL) {
        listenerList = g_list_sort(listenerList, status_listener_compare);
        item = g_list_first(listenerList);
    }

    /* Iterate the listeners. */
    while (statusesChanged && item) {
        StatusListener* listener = item->data;

        /* Call only if the listener is still in the table. */
        if (g_hash_table_contains(descriptor->listeners, listener)) {
            statuslistener_onStatusChanged(listener, descriptor->status, statusesChanged);
        }

        /* The above callback may have changes status again,
         * so make sure we consider the latest status state. */
        statusesChanged = descriptor->status ^ oldStatus;
        item = g_list_next(item);
    }

    if (listenerList != NULL) {
        g_list_free(listenerList);
    }
}

void legacydesc_adjustStatus(LegacyDescriptor* descriptor, Status status, gboolean doSetBits) {
    MAGIC_ASSERT(descriptor);

    Status oldStatus = descriptor->status;

    /* adjust our status as requested */
    if (doSetBits) {
        /* Set all bits indicated by status */
        descriptor->status |= status;
    } else {
        /* Unset all bits indicated by status */
        descriptor->status &= ~status;
    }

    /* Let helper handle the change. */
    _legacydesc_handleStatusChange(descriptor, oldStatus);
}

Status legacydesc_getStatus(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->status;
}

void legacydesc_addListener(LegacyDescriptor* descriptor, StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* We are storing a listener instance, so count the ref. */
    statuslistener_ref(listener);
    g_hash_table_insert(descriptor->listeners, listener, listener);
}

void legacydesc_removeListener(LegacyDescriptor* descriptor, StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* This will automatically call descriptorlistener_unref on the instance. */
    g_hash_table_remove(descriptor->listeners, listener);
}

gint legacydesc_getFlags(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->flags;
}

void legacydesc_setFlags(LegacyDescriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags = flags;
}

void legacydesc_addFlags(LegacyDescriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags |= flags;
}

void legacydesc_removeFlags(LegacyDescriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags &= ~flags;
}

void legacydesc_shutdownHelper(LegacyDescriptor* legacyDesc) {
    MAGIC_ASSERT(legacyDesc);

    if (legacyDesc->type == DT_EPOLL) {
        epoll_clearWatchListeners((Epoll*)legacyDesc);
    }
}

bool legacydesc_supportsSaRestart(LegacyDescriptor* legacyDesc) {
    switch (legacyDesc->type) {
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
            // TODO: false if a timeout has been set via setsockopt.
            return true;
        case DT_TIMER:
        case DT_EPOLL:
        case DT_FILE:
        case DT_EVENTD: return false;
        case DT_NONE:
            panic("Unexpected type DT_NONE");
            break;
            // no default, so compiler will force all cases to be handled.
    };
    panic("Bad type: %d", legacyDesc->type);
}
