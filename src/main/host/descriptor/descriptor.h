/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DESCRIPTOR_H_
#define SHD_DESCRIPTOR_H_

#include <glib.h>

#include "main/host/descriptor/descriptor_listener.h"
#include "main/host/descriptor/descriptor_status.h"
#include "main/utility/utility.h"

typedef enum _DescriptorType DescriptorType;
enum _DescriptorType {
    DT_NONE,
    DT_TCPSOCKET,
    DT_UDPSOCKET,
    DT_PIPE,
    DT_SOCKETPAIR,
    DT_EPOLL,
    DT_TIMER,
    DT_FILE,
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
gint descriptor_getHandle(Descriptor* descriptor);
gint* descriptor_getHandleReference(Descriptor* descriptor);

gint descriptor_getFlags(Descriptor* descriptor);
void descriptor_setFlags(Descriptor* descriptor, gint flags);
void descriptor_addFlags(Descriptor* descriptor, gint flags);

/*
 * One of the main functions of the descriptor is to track its poll status,
 * i.e., if it is readable, writable, etc. The adjustStatus function is used
 * throughout the codebase to maintain the correct status for descriptors.
 *
 * The statuses are tracked using the DescriptorStatus enum, which we treat
 * like a bitfield. Each bit represents a status type, and that status can
 * be either set or unset. The `status` arg represents which status(es) should
 * be adjusted, and the `doSetBits` arg specifies if the bit should be set or
 * unset.
 *
 * For example, a socket's readability is tracked with the DS_READABLE status.
 * When a socket has data and becomes readable, adjustStatus is called with
 * DS_READABLE as the status and doSetBits as TRUE. Once all data has been read,
 * adjustStatus is called with DS_READABLE as the status and doSetBits as FALSE.
 *
 * Multiple status bits can be set of unset at the same time.
 *
 * Whenever a call to adjustStatus causes a status transition (at least one
 * status bit flips), it will go through the set of listeners added with
 * addListener and call descriptorlistener_onStatusChanged on each one. The
 * listener will trigger notifications via callback functions if the listener is
 * configured to monitor a bit that flipped.
 */
void descriptor_adjustStatus(Descriptor* descriptor, DescriptorStatus status, gboolean doSetBits);

/* Gets the current status of the descriptor. */
DescriptorStatus descriptor_getStatus(Descriptor* descriptor);

/* Adds a listener that will get notified via descriptorlistener_onStatusChanged
 * on status transitions (bit flips).
 */
void descriptor_addListener(Descriptor* descriptor,
                            DescriptorListener* listener);

/* Remove the listener for our set of listeners that get notified on status
 * transitions (bit flips). */
void descriptor_removeListener(Descriptor* descriptor,
                               DescriptorListener* listener);

#endif /* SHD_DESCRIPTOR_H_ */
