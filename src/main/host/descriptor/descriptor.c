/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/descriptor.h"

#include <stddef.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor_listener.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

void descriptor_init(Descriptor* descriptor, DescriptorType type,
                     DescriptorFunctionTable* funcTable) {
    utility_assert(descriptor && funcTable);

    MAGIC_INIT(descriptor);
    MAGIC_INIT(funcTable);
    descriptor->funcTable = funcTable;
    descriptor->type = type;
    descriptor->listeners =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                              (GDestroyNotify)statuslistener_unref);
    descriptor->referenceCount = 1;

    debug("Descriptor %i has been initialized now", descriptor->handle);

    worker_countObject(OBJECT_TYPE_DESCRIPTOR, COUNTER_TYPE_NEW);
}

void descriptor_clear(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    if (descriptor->listeners) {
        g_hash_table_destroy(descriptor->listeners);
    }
    MAGIC_CLEAR(descriptor);
}

static void _descriptor_free(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    debug("Descriptor %i calling vtable free now", descriptor->handle);
    descriptor->funcTable->free(descriptor);

    worker_countObject(OBJECT_TYPE_DESCRIPTOR, COUNTER_TYPE_FREE);
}

void descriptor_ref(gpointer data) {
    Descriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->referenceCount)++;
    debug("Descriptor %i ref++ to %i", descriptor->handle,
          descriptor->referenceCount);
}

void descriptor_unref(gpointer data) {
    Descriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->referenceCount)--;
    debug("Descriptor %i ref-- to %i", descriptor->handle,
          descriptor->referenceCount);

    utility_assert(descriptor->referenceCount >= 0);
    if(descriptor->referenceCount == 0) {
        gint handle = descriptor->handle;
        _descriptor_free(descriptor);
    }
}

void descriptor_close(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);
    debug("Descriptor %i calling vtable close now", descriptor->handle);
    descriptor_adjustStatus(descriptor, DS_CLOSED, TRUE);
    if (descriptor->funcTable->close(descriptor) && descriptor->ownerProcess) {
        process_deregisterDescriptor(descriptor->ownerProcess, descriptor);
    }
}

gint descriptor_compare(const Descriptor* foo, const Descriptor* bar, gpointer user_data) {
    MAGIC_ASSERT(foo);
    MAGIC_ASSERT(bar);
    return foo->handle > bar->handle ? +1 : foo->handle == bar->handle ? 0 : -1;
}

DescriptorType descriptor_getType(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->type;
}

void descriptor_setHandle(Descriptor* descriptor, gint handle) {
    MAGIC_ASSERT(descriptor);
    descriptor->handle = handle;
}

gint descriptor_getHandle(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->handle;
}

void descriptor_setOwnerProcess(Descriptor* descriptor, Process* ownerProcess) {
    MAGIC_ASSERT(descriptor);
    descriptor->ownerProcess = ownerProcess;
}

Process* descriptor_getOwnerProcess(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->ownerProcess;
}

gint* descriptor_getHandleReference(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return &(descriptor->handle);
}

#ifdef DEBUG
static gchar* _descriptor_statusToString(DescriptorStatus ds) {
    GString* string = g_string_new(NULL);
    if(ds & DS_ACTIVE) {
        g_string_append_printf(string, "ACTIVE|");
    }
    if(ds & DS_READABLE) {
        g_string_append_printf(string, "READABLE|");
    }
    if(ds & DS_WRITABLE) {
        g_string_append_printf(string, "WRITEABLE|");
    }
    if(ds & DS_CLOSED) {
        g_string_append_printf(string, "CLOSED|");
    }
    if(string->len == 0) {
        g_string_append_printf(string, "NONE|");
    }
    g_string_truncate(string, string->len-1);
    return g_string_free(string, FALSE);
}
#endif

static void _descriptor_handleStatusChange(Descriptor* descriptor, DescriptorStatus oldStatus) {
    MAGIC_ASSERT(descriptor);

    /* Identify which bits changed, if any. */
    DescriptorStatus statusesChanged = descriptor->status ^ oldStatus;

    if (!statusesChanged) {
        return;
    }

#ifdef DEBUG
    gchar* before = _descriptor_statusToString(oldStatus);
    gchar* after = _descriptor_statusToString(descriptor->status);
    debug("Status changed on desc %i, from %s to %s", descriptor->handle, before, after);
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
            statuslistener_onStatusChanged(
                listener, descriptor->status, statusesChanged);
        }

        /* The above callback may have changes status again,
         * so make sure we consider the latest status state. */
        statusesChanged = descriptor->status ^ oldStatus;
        item = g_list_next(item);
    }

    g_list_free(listenerList);
}

void descriptor_adjustStatus(Descriptor* descriptor, DescriptorStatus status, gboolean doSetBits){
    MAGIC_ASSERT(descriptor);

    DescriptorStatus oldStatus = descriptor->status;

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

DescriptorStatus descriptor_getStatus(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->status;
}

void descriptor_addListener(Descriptor* descriptor,
                            StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* We are storing a listener instance, so count the ref. */
    statuslistener_ref(listener);
    g_hash_table_insert(descriptor->listeners, listener, listener);
}

void descriptor_removeListener(Descriptor* descriptor,
                               StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* This will automatically call descriptorlistener_unref on the instance. */
    g_hash_table_remove(descriptor->listeners, listener);
}

gint descriptor_getFlags(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->flags;
}

void descriptor_setFlags(Descriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags = flags;
}

void descriptor_addFlags(Descriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags |= flags;
}
