/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DESCRIPTOR_H_
#define SHD_DESCRIPTOR_H_

#include <glib.h>

#include "main/host/descriptor/descriptor_types.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"

/* Initialize the parent parts of a new descriptor subclass. This call should
 * be paired with a call to clear() before freeing the subclass object. */
void descriptor_init(LegacyDescriptor* descriptor, LegacyDescriptorType type,
                     DescriptorFunctionTable* funcTable);
/* Clear the bits that were initialized in init(). Following this call, the
 * descriptor becomes invalid and the subclass should be freed. */
void descriptor_clear(LegacyDescriptor* descriptor);

void descriptor_ref(gpointer data);
void descriptor_unref(gpointer data);
void descriptor_close(LegacyDescriptor* descriptor);
gint descriptor_compare(const LegacyDescriptor* foo, const LegacyDescriptor* bar, gpointer user_data);

void descriptor_setHandle(LegacyDescriptor* descriptor, gint handle);
gint descriptor_getHandle(LegacyDescriptor* descriptor);
void descriptor_setOwnerProcess(LegacyDescriptor* descriptor, Process* ownerProcess);
Process* descriptor_getOwnerProcess(LegacyDescriptor* descriptor);
LegacyDescriptorType descriptor_getType(LegacyDescriptor* descriptor);
gint* descriptor_getHandleReference(LegacyDescriptor* descriptor);

gint descriptor_getFlags(LegacyDescriptor* descriptor);
void descriptor_setFlags(LegacyDescriptor* descriptor, gint flags);
void descriptor_addFlags(LegacyDescriptor* descriptor, gint flags);

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
 * For example, a socket's readability is tracked with the STATUS_DESCRIPTOR_READABLE status.
 * When a socket has data and becomes readable, adjustStatus is called with
 * STATUS_DESCRIPTOR_READABLE as the status and doSetBits as TRUE. Once all data has been read,
 * adjustStatus is called with STATUS_DESCRIPTOR_READABLE as the status and doSetBits as FALSE.
 *
 * Multiple status bits can be set of unset at the same time.
 *
 * Whenever a call to adjustStatus causes a status transition (at least one
 * status bit flips), it will go through the set of listeners added with
 * addListener and call descriptorlistener_onStatusChanged on each one. The
 * listener will trigger notifications via callback functions if the listener is
 * configured to monitor a bit that flipped.
 */
void descriptor_adjustStatus(LegacyDescriptor* descriptor, Status status, gboolean doSetBits);

/* Gets the current status of the descriptor. */
Status descriptor_getStatus(LegacyDescriptor* descriptor);

/* Adds a listener that will get notified via descriptorlistener_onStatusChanged
 * on status transitions (bit flips).
 */
void descriptor_addListener(LegacyDescriptor* descriptor, StatusListener* listener);

/* Remove the listener for our set of listeners that get notified on status
 * transitions (bit flips). */
void descriptor_removeListener(LegacyDescriptor* descriptor, StatusListener* listener);

#endif /* SHD_DESCRIPTOR_H_ */
