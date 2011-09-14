/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SHMCABINET_INTERNAL_H_
#define SHMCABINET_INTERNAL_H_

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

#include "rwlock_mgr.h"

/**
 * This file is not meant to be included by anyone except shmcabinet.c/h.
 * Please include the shmcabinet.h header instead.
 *
 * The following is the memory outline for a shared-memory cabinet.
 *
 * shmcabinet_s
 *  |------------------|
 *  | cabinet header   | <-- sizeof(shmcabinet_s)
 *  |------------------|
 *  | slot 0 header    | <-- sizeof(shmcabinet_slot_s)
 *  |------------------|
 *  | slot 0 payload   | <-- payload size passed in constructor
 *  |------------------|
 *  | slot 1 header    |  etc.
 *  |------------------|
 *  | ...              |
 *  |------------------|
 *  | slot n payload   |
 *  |------------------|
 */

/* name format all cabinet shared memory, if this changes, update size below */
#define SHMCABINET_NAME_FORMAT "/shmcabinet-shm-%u-%u"

/* maximum size of a shmem name:
 * 20 for gchars, pad, and null, 20 for the maximum gchars required for 2 ugint32s */
#define SHMCABINET_NAME_MAX_SIZE 40

/* a slot is valid if the its valid tag matches this */
#define SHMCABINET_VALID 0xfedcba

/* represents an invalid offset. indicates a slot has been allocated. */
#define SHMCABINET_INVALID 0

enum shmcabinet_lockop {
	SHMCABINET_READLOCK, SHMCABINET_WRITELOCK, SHMCABINET_READUNLOCK, SHMCABINET_WRITEUNLOCK
};

/* inner structure that exists in a shared memory segment and holds payloads
 * whose size is specified during cabinet creation. the payload for each slot
 * will be held immediately following this structure in the shared memory space. */
typedef struct shmcabinet_slot_s {
	/* unique slot id inside a given cabinet */
	guint32 id;
	/* number of open references to this slot (reference count) */
	guint32 num_opened;
	/* offset to the next unallocated slot from the cabinet pointer */
	size_t next_slot_offset;
	/* a lock is stored in the first bytes of data. its size is stored here.
	 * lock protects num_opened and next_slot_offset only */
	/* if the slot is valid, this will be set to SHMCABINET_VALID */
	guint32 valid;
	gchar data[];
} shmcabinet_slot_t, *shmcabinet_slot_tp;

/* cabinet structure and associated control information.
 * the cabinet slots and their payloads immediately follow this
 * structure in shared memory. */
typedef struct shmcabinet_s {
	/* unique process id, combined with cabinet id to create unique shmem mappings */
	guint32 pid;
	/* unique cabinet id for this process */
	guint32 id;
	/* total size of the cabinet computed during construction */
	size_t size;
	/* number of open references to this cabinet (reference count) */
	guint32 num_opened;
	/* size of each slot, including header and payload */
	size_t slot_size;
	/* number of slots this cabinet holds, specified in the constructor */
	guint32 num_slots;
	/* number of allocated slots in this cabinet (reference count) */
	guint32 num_slots_allocated;
	/* offset to the first unallocated slot from the cabinet pointer */
	size_t head_slot_offset;
	/* a lock is stored in the first bytes of data. its size is stored here.
	 * lock protects num_opened, num_slots_allocated, and head_slot_offset only */
	size_t cabinet_lock_size;
	size_t slot_lock_size;
	/* if the cabinet is valid, this will be set to SHMCABINET_VALID */
	guint32 valid;
	gchar data[];
} shmcabinet_s;

/* creates a buffer with the given variable name that contains
 * the name of the shared memory file mapped in /dev/shm/ */
#define shmcabinet_NAME(bufname, process_id, cabinet_id) \
	gchar bufname[SHMCABINET_NAME_MAX_SIZE]; \
	snprintf(bufname, SHMCABINET_NAME_MAX_SIZE, SHMCABINET_NAME_FORMAT, process_id, cabinet_id)

/* returns a pointer to the mapped address of the head slot */
#define shmcabinet_HEAD(cabinet) \
	((shmcabinet_slot_tp)(((gchar*)cabinet) + (cabinet->head_slot_offset)))

/** returns the offset from the cabinet pointer to the slot given by slot_id */
#define shmcabinet_ID_TO_OFFSET(cabinet, slot_id) \
	((size_t)(sizeof(shmcabinet_t) + cabinet->cabinet_lock_size + ((slot_id) * (cabinet->slot_size))))

/* returns a pointer to the mapped address of the slot given by slot_id */
#define shmcabinet_ID_TO_SLOT(cabinet, slot_id) \
	((shmcabinet_slot_tp)(((gchar*)cabinet) + shmcabinet_ID_TO_OFFSET(cabinet, slot_id)))

/* returns the offset from the cabinet pointer to the given slot payload */
#define shmcabinet_PAYLOAD_TO_OFFSET(cabinet, payload) \
	((size_t)(((gchar*) payload) - ((gchar*) cabinet)))

/* returns a pointer to the mapped address of the slot of the given payload */
#define shmcabinet_PAYLOAD_TO_SLOT(cabinet, payload) \
	((shmcabinet_slot_tp)(((gchar*) payload) - cabinet->slot_lock_size - sizeof(shmcabinet_slot_t)))

#define shmcabinet_SLOT_TO_LOCK(slot) \
	((rwlock_mgr_tp) (((gchar*) slot) + sizeof(shmcabinet_slot_t)))

#define shmcabinet_SLOT_TO_PAYLOAD(cabinet, slot) \
	((gpointer ) (((gchar*) slot) + sizeof(shmcabinet_slot_t) + cabinet->slot_lock_size))

#define shmcabinet_CABINET_TO_LOCK(cabinet) \
	((rwlock_mgr_tp) (((gchar*) cabinet) + sizeof(shmcabinet_t)))

#endif /* SHMCABINET_INTERNAL_H_ */
