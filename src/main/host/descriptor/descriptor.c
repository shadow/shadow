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
#include "main/utility/utility.h"
#include "support/logger/logger.h"

void descriptor_init(Descriptor* descriptor, DescriptorType type,
        DescriptorFunctionTable* funcTable, gint handle) {
    utility_assert(descriptor && funcTable);

    MAGIC_INIT(descriptor);
    MAGIC_INIT(funcTable);
    descriptor->funcTable = funcTable;
    descriptor->handle = handle;
    descriptor->type = type;
    descriptor->listeners =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                              (GDestroyNotify)descriptorlistener_unref);
    descriptor->referenceCount = 1;

    debug("Descriptor %i has been initialized now", descriptor->handle);

    worker_countObject(OBJECT_TYPE_DESCRIPTOR, COUNTER_TYPE_NEW);
}

static void _descriptor_free(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    if (descriptor->listeners) {
        g_hash_table_destroy(descriptor->listeners);
    }

    debug("Descriptor %i calling vtable free now", descriptor->handle);

    descriptor->funcTable->free(descriptor);

    /* Clear *after* free, since the above call may access our content. */
    MAGIC_CLEAR(descriptor);

    worker_countObject(OBJECT_TYPE_DESCRIPTOR, COUNTER_TYPE_FREE);
}

void descriptor_ref(gpointer data) {
    Descriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    debug("Descriptor %i, refcount before ref++ is %i", descriptor->handle, descriptor->referenceCount);

    (descriptor->referenceCount)++;
}

void descriptor_unref(gpointer data) {
    Descriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    debug("Descriptor %i, refcount before ref-- is %i", descriptor->handle, descriptor->referenceCount);

    (descriptor->referenceCount)--;
    utility_assert(descriptor->referenceCount >= 0);
    if(descriptor->referenceCount == 0) {
        gint handle = descriptor->handle;
        _descriptor_free(descriptor);
        host_returnHandleHack(handle);
    }
}

void descriptor_close(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);
    debug("Descriptor %i calling vtable close now", descriptor->handle);
    descriptor_adjustStatus(descriptor, DS_CLOSED, TRUE);
    descriptor->funcTable->close(descriptor);
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

gint descriptor_getHandle(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->handle;
}

gint* descriptor_getHandleReference(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return &(descriptor->handle);
}

static void _descriptor_handleStatusChange(gpointer object, gpointer argument) {
    Descriptor* descriptor = object;
    DescriptorStatus oldStatus = (DescriptorStatus)GPOINTER_TO_INT(argument);
    MAGIC_ASSERT(descriptor);

    /* Identify which bits changed since this task was queued. */
    DescriptorStatus statusesChanged = descriptor->status ^ oldStatus;

    if (!statusesChanged) {
        return;
    }

    /* Tell our listeners there was some activity on this descriptor.
     * We can't use an iterator here, because the listener table may
     * be modified in the body of the while loop below, in the onStatusChanged
     * callback. Instead we get a list of the keys and do lookups on those.*/
    GList* listenerList = g_hash_table_get_keys(descriptor->listeners);

    /* Iterate the listeners. */
    GList* item = g_list_first(listenerList);
    while (statusesChanged && item) {
        DescriptorListener* listener = item->data;

        /* Call only if the listener is still in the table. */
        if (g_hash_table_contains(descriptor->listeners, listener)) {
            descriptorlistener_onStatusChanged(
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

    /* Identify which bits changed */
    DescriptorStatus statusesChanged = descriptor->status ^ oldStatus;

    if (statusesChanged) {
        /* We execute the handler via a task, to make sure whatever
         * code called adjustStatus finishes it's logic first before
         * the listener callbacks are executed and potentially change
         * the state of the descriptor again. */
        Task* handleStatusChange =
            task_new(_descriptor_handleStatusChange, descriptor,
                     GINT_TO_POINTER(oldStatus), descriptor_unref, NULL);
        worker_scheduleTask(handleStatusChange, 0);

        /* The descriptor will be unreffed after the task executes. */
        descriptor_ref(descriptor);

        /* A ref to the task is held by the event in the scheduler.
         * We don't need our reference anymore. */
        task_unref(handleStatusChange);
    }
}

DescriptorStatus descriptor_getStatus(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->status;
}

void descriptor_addListener(Descriptor* descriptor,
                            DescriptorListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* We are storing a listener instance, so count the ref. */
    descriptorlistener_ref(listener);
    g_hash_table_insert(descriptor->listeners, listener, listener);
}

void descriptor_removeListener(Descriptor* descriptor,
                               DescriptorListener* listener) {
    MAGIC_ASSERT(descriptor);
    /* This will automatically call descriptorlistener_ref on the instance. */
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
