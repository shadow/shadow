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


/*
 * shmcabinet.h
 *
 * POSIX shared memory data structure. See "man shm_overview", link with -lrt.
 *
 * There are two options for synchronizing shared memory:
 *   semaphores (default) - See "man sem_overview" (no extra compile flags required)
 *   pthread_rwlock - See "man pthread_rwlock_init" and friends, also link with -lpthread, compile with -D_XOPEN_SOURCE=600 -D_SHMCABINET_PTHREAD_RWLOCK
 *   custom_rwlock - alternative to pthread, compile with -D_SHMCABINET_CUSTOM_RWLOCK
 *
 * The user should not attempt to obtain a readlock and writelock at the same
 * time, or a deadlock will occur. If the user is using a rwlock, then the
 * same thread or process should not attempt to obtain two writelocks at the
 * same time. If the user is using semaphores, then the same thread or process
 * should not attempt to obtain two writelocks OR two readlocks at the same time.
 *
 *  Created on: Jan 24, 2011
 *      Author: rob
 */

#ifndef SHMCABINET_H_
#define SHMCABINET_H_

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

#include "shmcabinet_internal.h"

/* represents an invalid slot_id. indicates an error.
 * todo: more detailed error codes. */
#define SHMCABINET_ERROR UINT32_MAX

/* indicates success */
#define SHMCABINET_SUCCESS 0

/* the maximum number of slots allowed */
#define SHMCABINET_MAX_SLOTS UINT32_MAX - 1

/* a structure that contains the information needed to open a
 * mapping to a previously created shmcabinet. */
typedef struct shmcabinet_info_s {
	guint32 process_id;
	guint32 cabinet_id;
	size_t cabinet_size;
} shmcabinet_info_t, *shmcabinet_info_tp;

/* main cabinet structure. cabinet members should be accessed through the
 * interface defined in this file, and not be accessed directly. */
typedef struct shmcabinet_s shmcabinet_t, *shmcabinet_tp;

/* maps a new cabinet ginto shared memory. the cabinet has num_slots slots
 * that can each hold payloads of slot_payload_size bytes in size. num_slots must
 * not exceed SHMCABINET_MAX_SLOTS. caller must specify a valid lock_type for
 * the cabinet and the slot locks.
 * returns a pointer to the mapped cabinet, or NULL if num_slots exceeds
 * SHMCABINET_MAX_SLOTS or there was an error. */
shmcabinet_tp shmcabinet_create(guint32 num_slots, size_t slot_payload_size,
		enum rwlock_mgr_type cabinet_lock_type, enum rwlock_mgr_type slot_lock_type);

/* maps a previously created cabinet ginto shared memory. */
shmcabinet_tp shmcabinet_map(guint32 process_id, guint32 cabinet_id, size_t cabinet_size);

/* removes the mapped cabinet from the address space of the calling process.
 * if no other references to the cabinet exist, it is destroyed and unlinked.
 * returns SHMCABINET_ERROR on error, or SHMCABINET_SUCCESS on success. */
guint32 shmcabinet_unmap(shmcabinet_tp cabinet);

/* allocates and opens an available slot from the given cabinet.
 * returns NULL if there was an error or there are no slots available for
 * allocation. otherwise a pointer to the uncleared slot payload is returned. */
gpointer shmcabinet_allocate(shmcabinet_tp cabinet);

/* opens the slot given by slot_id in the cabinet. this slot must be closed
 * with a call to shmcabinet_close before its resources can be released.
 * returns NULL if there was problems opening the slot, otherwise a pointer
 * to the mapped address of the slot's payload is returned. */
gpointer shmcabinet_open(shmcabinet_tp cabinet, guint32 slot_id);

/* closes the reference to the slot holding the payload given by the pointer
 * payload. the slot is deallocated if no further references to it remain.
 * returns SHMCABINET_ERROR on error, or SHMCABINET_SUCCESS on success. */
guint32 shmcabinet_close(shmcabinet_tp cabinet, gpointer payload);

/* locks the slot that holds the payload given by the pointer payload. reads are
 * allowed, but no other writes to the slot are allowed until the slot is unlocked
 * with shmcabinet_unlock.
 * returns SHMCABINET_ERROR on error, or SHMCABINET_SUCCESS on success. */
guint32 shmcabinet_readlock(shmcabinet_tp cabinet, gpointer payload);

/* unlocks the slot that holds the payload given by the pointer payload.
 * if the slot is not read-locked, this call has no effect. */
guint32 shmcabinet_readunlock(shmcabinet_tp cabinet, gpointer payload);

/* locks the slot that holds the payload given by the pointer payload. no other
 * reads or writes to the slot are allowed until the slot is unlocked
 * with shmcabinet_unlock.
 * returns SHMCABINET_ERROR on error, or SHMCABINET_SUCCESS on success. */
guint32 shmcabinet_writelock(shmcabinet_tp cabinet, gpointer payload);

/* unlocks the slot that holds the payload given by the pointer payload.
 * if the slot is not write-locked, this call has no effect. */
guint32 shmcabinet_writeunlock(shmcabinet_tp cabinet, gpointer payload);

/* populates the given info structure with the info necessary for an unrelated
 * process to map this cabinet. */
guint32 shmcabinet_get_info(shmcabinet_tp cabinet, shmcabinet_info_tp info);

/* returns the id of the slot holding the payload, or SHMCABINET_ERROR if
 * there was an error or the slot is not allocated */
guint32 shmcabinet_get_id(shmcabinet_tp cabinet, gpointer payload);

/* returns the number of unallocated slots, or 0 if cabinet is NULL. */
guint32 shmcabinet_slots_available(shmcabinet_tp cabinet);

/* returns 1 if cabinet is not NULL and no slots are allocated, 0 otherwise. */
guint8 shmcabinet_is_empty(shmcabinet_tp cabinet);

#endif /* SHMCABINET_H_ */
