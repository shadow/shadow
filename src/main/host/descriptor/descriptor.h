/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DESCRIPTOR_H_
#define SHD_DESCRIPTOR_H_

#include <glib.h>

#include "main/host/descriptor/descriptor_listener.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/process.h"

/* Initialize the parent parts of a new descriptor subclass. This call should
 * be paired with a call to clear() before freeing the subclass object. */
void descriptor_init(Descriptor* descriptor, DescriptorType type,
                     DescriptorFunctionTable* funcTable);
/* Clear the bits that were initialized in init(). Following this call, the
 * descriptor becomes invalid and the subclass should be freed. */
void descriptor_clear(Descriptor* descriptor);

void descriptor_ref(gpointer data);
void descriptor_unref(gpointer data);
void descriptor_close(Descriptor* descriptor);
gint descriptor_compare(const Descriptor* foo, const Descriptor* bar, gpointer user_data);

void descriptor_setHandle(Descriptor* descriptor, gint handle);
gint descriptor_getHandle(Descriptor* descriptor);
void descriptor_setOwnerProcess(Descriptor* descriptor, Process* ownerProcess);
Process* descriptor_getOwnerProcess(Descriptor* descriptor);
DescriptorType descriptor_getType(Descriptor* descriptor);
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
                            StatusListener* listener);

/* Remove the listener for our set of listeners that get notified on status
 * transitions (bit flips). */
void descriptor_removeListener(Descriptor* descriptor,
                               StatusListener* listener);

#endif /* SHD_DESCRIPTOR_H_ */
