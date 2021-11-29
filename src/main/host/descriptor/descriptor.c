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

void descriptor_init(LegacyDescriptor* descriptor, LegacyDescriptorType type,
                     DescriptorFunctionTable* funcTable) {
    utility_assert(descriptor && funcTable);

    MAGIC_INIT(descriptor);
    MAGIC_INIT(funcTable);
    descriptor->funcTable = funcTable;
    descriptor->type = type;
    descriptor->handle = -1;
    descriptor->listeners = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)statuslistener_unref);
    descriptor->refCountStrong = 1;
    descriptor->refCountWeak = 0;

    trace("Descriptor %i has been initialized now", descriptor->handle);

    worker_count_allocation(LegacyDescriptor);
}

void descriptor_clear(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    if (descriptor->listeners) {
        g_hash_table_destroy(descriptor->listeners);
    }
    MAGIC_CLEAR(descriptor);
}

static void _descriptor_cleanup(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    if (descriptor->funcTable->cleanup) {
        trace("Descriptor %i calling vtable cleanup now", descriptor->handle);
        descriptor->funcTable->cleanup(descriptor);
    }
}

static void _descriptor_free(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    trace("Descriptor %i calling vtable free now", descriptor->handle);
    descriptor->funcTable->free(descriptor);

    worker_count_deallocation(LegacyDescriptor);
}

void descriptor_ref(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    // should not increment the strong count when there are only weak references left
    utility_assert(descriptor->refCountStrong > 0);

    (descriptor->refCountStrong)++;
    trace("Descriptor %i strong_ref++ to %i (weak_ref=%i)", descriptor->handle,
          descriptor->refCountStrong, descriptor->refCountWeak);
}

void descriptor_unref(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountStrong)--;
    trace("Descriptor %i strong_ref-- to %i (weak_ref=%i)", descriptor->handle,
          descriptor->refCountStrong, descriptor->refCountWeak);

    utility_assert(descriptor->refCountStrong >= 0);

    if (descriptor->refCountStrong > 0) {
        // there are strong references, so do nothing
        return;
    }

    if (descriptor->refCountWeak > 0) {
        // this was the last strong reference, but there are weak references, so cleanup only
        trace("Descriptor %i kept alive by weak count of %d", descriptor->handle,
              descriptor->refCountWeak);

        // create a temporary weak reference to prevent the _descriptor_cleanup() from calling
        // descriptor_unrefWeak() and running the _descriptor_free() while still running the
        // _descriptor_cleanup()
        descriptor_refWeak(descriptor);
        _descriptor_cleanup(descriptor);
        descriptor_unrefWeak(descriptor);

        return;
    }

    // this was the last strong reference and no weak references, so cleanup and free
    _descriptor_cleanup(descriptor);
    _descriptor_free(descriptor);
}

void descriptor_refWeak(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountWeak)++;
    trace("Descriptor %i weak_ref++ to %i (strong_ref=%i)", descriptor->handle,
          descriptor->refCountWeak, descriptor->refCountStrong);
}

void descriptor_unrefWeak(gpointer data) {
    LegacyDescriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountWeak)--;
    trace("Descriptor %i weak_ref-- to %i (strong_ref=%i)", descriptor->handle,
          descriptor->refCountWeak, descriptor->refCountStrong);

    utility_assert(descriptor->refCountWeak >= 0);

    if (descriptor->refCountStrong > 0 || descriptor->refCountWeak > 0) {
        // there are references (strong or weak), so do nothing
        return;
    }

    // this was the last weak reference and no strong references, so we should free
    // _descriptor_cleanup() should have been called earlier when the strong count reached 0
    _descriptor_free(descriptor);
}

void descriptor_close(LegacyDescriptor* descriptor, Host* host) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    // if it's already closed, exit early
    if ((descriptor_getStatus(descriptor) & STATUS_DESCRIPTOR_CLOSED) != 0) {
        warning("Attempting to close an already-closed descriptor");
        return;
    }

    trace("Descriptor %i calling vtable close now", descriptor->handle);
    descriptor_adjustStatus(descriptor, STATUS_DESCRIPTOR_CLOSED, TRUE);

    descriptor->funcTable->close(descriptor, host);
}

gint descriptor_compare(const LegacyDescriptor* foo, const LegacyDescriptor* bar, gpointer user_data) {
    MAGIC_ASSERT(foo);
    MAGIC_ASSERT(bar);
    return foo->handle > bar->handle ? +1 : foo->handle == bar->handle ? 0 : -1;
}

LegacyDescriptorType descriptor_getType(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->type;
}

void descriptor_setHandle(LegacyDescriptor* descriptor, gint handle) {
    MAGIC_ASSERT(descriptor);
    descriptor->handle = handle;
}

gint descriptor_getHandle(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->handle;
}

void descriptor_setOwnerProcess(LegacyDescriptor* descriptor, Process* ownerProcess) {
    MAGIC_ASSERT(descriptor);
    descriptor->ownerProcess = ownerProcess;
}

Process* descriptor_getOwnerProcess(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->ownerProcess;
}

gint* descriptor_getHandleReference(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return &(descriptor->handle);
}

#ifdef DEBUG
static gchar* _descriptor_statusToString(Status ds) {
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

static void _descriptor_handleStatusChange(LegacyDescriptor* descriptor, Status oldStatus) {
    MAGIC_ASSERT(descriptor);

    /* Identify which bits changed, if any. */
    Status statusesChanged = descriptor->status ^ oldStatus;

    if (!statusesChanged) {
        return;
    }

#ifdef DEBUG
    gchar* before = _descriptor_statusToString(oldStatus);
    gchar* after = _descriptor_statusToString(descriptor->status);
    trace("Status changed on desc %i, from %s to %s", descriptor->handle, before, after);
    g_free(before);
    g_free(after);
#endif

    /* Tell our listeners there was some activity on this descriptor.
     * We can't use an iterator here, because the listener table may
     * be modified in the body of the while loop below, in the onStatusChanged
     * callback. Instead we get a list of the keys and do lookups on those.*/
    GList* listenerList = g_hash_table_get_keys(descriptor->listeners);

    /* Iterate the listeners. */
    GList* item = g_list_first(listenerList);
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

    g_list_free(listenerList);
}

void descriptor_adjustStatus(LegacyDescriptor* descriptor, Status status, gboolean doSetBits) {
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
    _descriptor_handleStatusChange(descriptor, oldStatus);
}

Status descriptor_getStatus(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->status;
}

void descriptor_addListener(LegacyDescriptor* descriptor, StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* We are storing a listener instance, so count the ref. */
    statuslistener_ref(listener);
    g_hash_table_insert(descriptor->listeners, listener, listener);
}

void descriptor_removeListener(LegacyDescriptor* descriptor, StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* This will automatically call descriptorlistener_unref on the instance. */
    g_hash_table_remove(descriptor->listeners, listener);
}

gint descriptor_getFlags(LegacyDescriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->flags;
}

void descriptor_setFlags(LegacyDescriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags = flags;
}

void descriptor_addFlags(LegacyDescriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags |= flags;
}

void descriptor_removeFlags(LegacyDescriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags &= ~flags;
}

void descriptor_shutdownHelper(LegacyDescriptor* legacyDesc) {
    MAGIC_ASSERT(legacyDesc);

    if (legacyDesc->type == DT_EPOLL) {
        epoll_clearWatchListeners((Epoll*)legacyDesc);
    }
}
