/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/descriptor.h"

#include <stddef.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/shd-descriptor-listener.h"
#include "main/host/host.h"
#include "main/utility/utility.h"

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

    worker_countObject(OBJECT_TYPE_DESCRIPTOR, COUNTER_TYPE_NEW);
}

static void _descriptor_free(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    if (descriptor->listeners) {
        g_hash_table_destroy(descriptor->listeners);
    }

    MAGIC_CLEAR(descriptor);
    descriptor->funcTable->free(descriptor);

    worker_countObject(OBJECT_TYPE_DESCRIPTOR, COUNTER_TYPE_FREE);
}

void descriptor_ref(gpointer data) {
    Descriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->referenceCount)++;
}

void descriptor_unref(gpointer data) {
    Descriptor* descriptor = data;
    MAGIC_ASSERT(descriptor);

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

gint* descriptor_getHandleReference(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return &(descriptor->handle);
}

void descriptor_adjustStatus(Descriptor* descriptor, DescriptorStatus status, gboolean doSetBits){
    MAGIC_ASSERT(descriptor);

    DescriptorStatus statusesChanged = DS_NONE;

    /* adjust our status as requested */
    if(doSetBits) {
        if((status & DS_ACTIVE) && !(descriptor->status & DS_ACTIVE)) {
            /* status changed - is now active */
            statusesChanged |= DS_ACTIVE;
            descriptor->status |= DS_ACTIVE;
        }
        if((status & DS_READABLE) && !(descriptor->status & DS_READABLE)) {
            /* status changed - is now readable */
            statusesChanged |= DS_READABLE;
            descriptor->status |= DS_READABLE;
        }
        if((status & DS_WRITABLE) && !(descriptor->status & DS_WRITABLE)) {
            /* status changed - is now writable */
            statusesChanged |= DS_WRITABLE;
            descriptor->status |= DS_WRITABLE;
        }
        if((status & DS_CLOSED) && !(descriptor->status & DS_CLOSED)) {
            /* status changed - is now closed to user */
            statusesChanged |= DS_CLOSED;
            descriptor->status |= DS_CLOSED;
        }
    } else {
        if((status & DS_ACTIVE) && (descriptor->status & DS_ACTIVE)) {
            /* status changed - no longer active */
            statusesChanged |= DS_ACTIVE;
            descriptor->status &= ~DS_ACTIVE;
        }
        if((status & DS_READABLE) && (descriptor->status & DS_READABLE)) {
            /* status changed - no longer readable */
            statusesChanged |= DS_READABLE;
            descriptor->status &= ~DS_READABLE;
        }
        if((status & DS_WRITABLE) && (descriptor->status & DS_WRITABLE)) {
            /* status changed - no longer writable */
            statusesChanged |= DS_WRITABLE;
            descriptor->status &= ~DS_WRITABLE;
        }
        if((status & DS_CLOSED) && (descriptor->status & DS_CLOSED)) {
            /* status changed - no longer closed to user */
            statusesChanged |= DS_CLOSED;
            descriptor->status &= ~DS_CLOSED;
        }
    }

    /* tell our listeners there was some activity on this descriptor */
    if (statusesChanged != DS_NONE) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, descriptor->listeners);

        while (g_hash_table_iter_next(&iter, &key, &value)) {
            DescriptorListener* listener = value;
            descriptorlistener_onStatusChanged(listener, statusesChanged);
        }
    }
}

DescriptorStatus descriptor_getStatus(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);

    DescriptorStatus status = DS_NONE;

    if(descriptor->status & DS_ACTIVE) {
        status |= DS_ACTIVE;
    }
    if(descriptor->status & DS_READABLE) {
        status |= DS_READABLE;
    }
    if(descriptor->status & DS_WRITABLE) {
        status |= DS_WRITABLE;
    }
    if(descriptor->status & DS_CLOSED) {
        status |= DS_CLOSED;
    }

    return status;
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
