/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DESCRIPTOR_H_
#define SHD_DESCRIPTOR_H_

#include <glib.h>

#include "core/support/shd-definitions.h"

typedef enum _DescriptorType DescriptorType;
enum _DescriptorType {
    DT_TCPSOCKET, DT_UDPSOCKET, DT_PIPE, DT_SOCKETPAIR, DT_EPOLL, DT_TIMER
};

typedef enum _DescriptorStatus DescriptorStatus;
enum _DescriptorStatus {
    DS_NONE = 0,
    /* ok to notify user as far as we know, socket is ready.
     * o/w never notify user (b/c they e.g. closed the socket or did not accept yet) */
    DS_ACTIVE = 1 << 0,
    /* can be read, i.e. there is data waiting for user */
    DS_READABLE = 1 << 1,
    /* can be written, i.e. there is available buffer space */
    DS_WRITABLE = 1 << 2,
    /* user already called close */
    DS_CLOSED = 1 << 3,
};

typedef struct _Descriptor Descriptor;
typedef struct _DescriptorFunctionTable DescriptorFunctionTable;

/* required functions */
typedef void (*DescriptorFunc)(Descriptor* descriptor);

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _DescriptorFunctionTable {
    DescriptorFunc close;
    DescriptorFunc free;
    MAGIC_DECLARE;
};

struct _Descriptor {
    DescriptorFunctionTable* funcTable;
    gint handle;
    DescriptorType type;
    DescriptorStatus status;
    GHashTable* epollListeners;
    gint referenceCount;
    gint flags;
    MAGIC_DECLARE;
};

void descriptor_init(Descriptor* descriptor, DescriptorType type,
        DescriptorFunctionTable* funcTable, gint handle);
void descriptor_ref(gpointer data);
void descriptor_unref(gpointer data);
void descriptor_close(Descriptor* descriptor);
gint descriptor_compare(const Descriptor* foo, const Descriptor* bar, gpointer user_data);

DescriptorType descriptor_getType(Descriptor* descriptor);
gint* descriptor_getHandleReference(Descriptor* descriptor);

void descriptor_adjustStatus(Descriptor* descriptor, DescriptorStatus status, gboolean doSetBits);
DescriptorStatus descriptor_getStatus(Descriptor* descriptor);

void descriptor_addEpollListener(Descriptor* descriptor, Descriptor* epoll);
void descriptor_removeEpollListener(Descriptor* descriptor, Descriptor* epoll);

gint descriptor_getFlags(Descriptor* descriptor);
void descriptor_setFlags(Descriptor* descriptor, gint flags);

#endif /* SHD_DESCRIPTOR_H_ */
