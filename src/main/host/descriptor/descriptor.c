/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/descriptor.h"

#include <stddef.h>

#include "main/core/worker.h"
#include "main/core/support/object_counter.h"
#include "main/host/descriptor/epoll.h"
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
    descriptor->epollListeners = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, descriptor_unref);
    descriptor->referenceCount = 1;

    worker_countObject(OBJECT_TYPE_DESCRIPTOR, COUNTER_TYPE_NEW);
}

static void _descriptor_free(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    if(descriptor->epollListeners) {
        g_hash_table_destroy(descriptor->epollListeners);
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

static void _descriptor_notifyEpollListener(gpointer key, gpointer value, gpointer user_data) {
    Descriptor* descriptor = user_data;
    Epoll* epoll = value;
    epoll_descriptorStatusChanged(epoll, descriptor);
}

void descriptor_adjustStatus(Descriptor* descriptor, DescriptorStatus status, gboolean doSetBits){
    MAGIC_ASSERT(descriptor);

    /* adjust our status as requested */
    if(doSetBits) {
        if((status & DS_ACTIVE) && !(descriptor->status & DS_ACTIVE)) {
            /* status changed - is now active */
            descriptor->status |= DS_ACTIVE;
        }
        if((status & DS_READABLE) && !(descriptor->status & DS_READABLE)) {
            /* status changed - is now readable */
            descriptor->status |= DS_READABLE;
        }
        if((status & DS_WRITABLE) && !(descriptor->status & DS_WRITABLE)) {
            /* status changed - is now writable */
            descriptor->status |= DS_WRITABLE;
        }
        if((status & DS_CLOSED) && !(descriptor->status & DS_CLOSED)) {
            /* status changed - is now closed to user */
            descriptor->status |= DS_CLOSED;
        }
    } else {
        if((status & DS_ACTIVE) && (descriptor->status & DS_ACTIVE)) {
            /* status changed - no longer active */
            descriptor->status &= ~DS_ACTIVE;
        }
        if((status & DS_READABLE) && (descriptor->status & DS_READABLE)) {
            /* status changed - no longer readable */
            descriptor->status &= ~DS_READABLE;
        }
        if((status & DS_WRITABLE) && (descriptor->status & DS_WRITABLE)) {
            /* status changed - no longer writable */
            descriptor->status &= ~DS_WRITABLE;
        }
        if((status & DS_CLOSED) && (descriptor->status & DS_CLOSED)) {
            /* status changed - no longer closed to user */
            descriptor->status &= ~DS_CLOSED;
        }
    }

    /* tell our epoll listeners their was some activity on this descriptor */
    g_hash_table_foreach(descriptor->epollListeners, _descriptor_notifyEpollListener, descriptor);
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

void descriptor_addEpollListener(Descriptor* descriptor, Descriptor* epoll) {
    MAGIC_ASSERT(descriptor);
    /* we are string the epoll instance, so increase the ref */
    descriptor_ref(epoll);
    g_hash_table_insert(descriptor->epollListeners, &epoll->handle, epoll);
}

void descriptor_removeEpollListener(Descriptor* descriptor, Descriptor* epoll) {
    MAGIC_ASSERT(descriptor);
    /* this will automatically call descriptor_unref on the epoll instance */
    g_hash_table_remove(descriptor->epollListeners, &epoll->handle);
}

gint descriptor_getFlags(Descriptor* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->flags;
}

void descriptor_setFlags(Descriptor* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags = flags;
}
