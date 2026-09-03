/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DESCRIPTOR_H_
#define SHD_DESCRIPTOR_H_

#include <glib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "main/bindings/c/bindings-opaque.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"

/* Initialize the parent parts of a new descriptor subclass. This call should
 * be paired with a call to clear() before freeing the subclass object. */
void legacyfile_init(LegacyFile* descriptor, LegacyFileType type,
                     LegacyFileFunctionTable* funcTable);
/* Clear the bits that were initialized in init(). Following this call, the
 * descriptor becomes invalid and the subclass should be freed. */
void legacyfile_clear(LegacyFile* descriptor);

void legacyfile_ref(gpointer data);
void legacyfile_unref(gpointer data);
void legacyfile_refWeak(gpointer data);
void legacyfile_unrefWeak(gpointer data);
void legacyfile_close(LegacyFile* descriptor, const Host* host);
int legacyfile_fstat(LegacyFile* descriptor, struct linux_stat* statbuf);
off_t legacyfile_lseek(LegacyFile* descriptor, off_t offset, int whence);

const RootedRefCell_StateEventSource* legacyfile_getEventSource(LegacyFile* descriptor);

LegacyFileType legacyfile_getType(LegacyFile* descriptor);

/* Get the "access mode" oflags. See open(2). */
gint legacyfile_getAccessModeFlags(LegacyFile* descriptor);

/* Get the "file status" oflags. See open(2). */
gint legacyfile_getFileStatusFlags(LegacyFile* descriptor);
/* Set the "file status" oflags. See open(2). */
void legacyfile_setFileStatusFlags(LegacyFile* descriptor, gint flags);

/* Should be called exactly once, generally by the constructor of the specific file type.
 * Ideally we'd make these a parameter to `legacyfile_init` instead, but that'd take
 * more significantly more refactoring.
 */
void legacyfile_setAccessAndCreationFlags(LegacyFile* descriptor, gint flags);

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
void legacyfile_adjustStatus(LegacyFile* descriptor, FileState status, gboolean doSetBits,
                             FileSignals signals);

/* Gets the current status of the descriptor. */
FileState legacyfile_getStatus(LegacyFile* descriptor);

/* Adds a listener that will get notified via descriptorlistener_onStatusChanged
 * on status transitions (bit flips).
 */
void legacyfile_addListener(LegacyFile* descriptor, StatusListener* listener);

/* Remove the listener for our set of listeners that get notified on status
 * transitions (bit flips). */
void legacyfile_removeListener(LegacyFile* descriptor, StatusListener* listener);

/* Whether the descriptor's operations are restartable in conjunction with
 * SA_RESTART. See signal(7).
 */
bool legacyfile_supportsSaRestart(LegacyFile* legacyDesc);

#endif /* SHD_DESCRIPTOR_H_ */
