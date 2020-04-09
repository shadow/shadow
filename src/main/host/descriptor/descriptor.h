/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DESCRIPTOR_H_
#define SHD_DESCRIPTOR_H_

#include <glib.h>

#include "main/host/descriptor/shd-descriptor-status.h"
#include "main/host/descriptor/shd-descriptor-listener.h"
#include "main/utility/utility.h"

typedef enum _DescriptorType DescriptorType;
enum _DescriptorType {
    DT_TCPSOCKET, DT_UDPSOCKET, DT_PIPE, DT_SOCKETPAIR, DT_EPOLL, DT_TIMER
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
    GHashTable* listeners;
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

void descriptor_addListener(Descriptor* descriptor,
                            DescriptorListener* listener);
void descriptor_removeListener(Descriptor* descriptor,
                               DescriptorListener* listener);

gint descriptor_getFlags(Descriptor* descriptor);
void descriptor_setFlags(Descriptor* descriptor, gint flags);

#endif /* SHD_DESCRIPTOR_H_ */
