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

#ifndef SHMCABINET_MGR_H_
#define SHMCABINET_MGR_H_

#include <glib.h>
#include <stdint.h>
#include <stddef.h>
#include <glib-2.0/glib.h>

#include "shmcabinet.h"
#include "rwlock_mgr.h"

typedef struct shm_s {
	shmcabinet_tp cabinet;
	shmcabinet_info_t info;
	guint32 references;
	guint8 owned;
} shm_t, *shm_tp;

typedef struct shm_item_s {
	shm_tp shm;
	guint32 slot_id;
	guint32 num_readlocks;
	guint32 num_writelocks;
	gpointer payload;
} shm_item_t, *shm_item_tp;

typedef struct shmcabinet_mgr_s {
	enum rwlock_mgr_type cabinet_lock_type;
	enum rwlock_mgr_type slot_lock_type;
	guint32 payloads_per_cabinet;
	guint32 min_payloads_threshold;
	size_t payload_size;
	GQueue *shm_owned_available;
	GHashTable *shm_owned;
	GHashTable *shm_unowned;
} shmcabinet_mgr_t, *shmcabinet_mgr_tp;

/* creates a new smc_mgr that can store payloads of payload_size in shared memory.
 * each shared memory segment can store payloads_per_cabinet items.
 * the mgr dynamically maps and unmaps new shared memory as necessary. the unmap
 * policy is such that an owned cabinet - one that this mgr created - is not unmapped
 * if it reduces the total number of allocatable cabinet slots below
 * (unmap_threshold*payloads_per_cabinet) slots.
 */
shmcabinet_mgr_tp shmcabinet_mgr_create(size_t payload_size, guint32 payloads_per_cabinet, guint32 unmap_threshold,
		enum rwlock_mgr_type cabinet_lock_type, enum rwlock_mgr_type slot_lock_type);

/* destroys a previously created smc_mgr by closing and unmapping all
 * references to shared memory. all memory for shm_items that were allocated
 * but not freed before this call will be lost (leaked).
 */
void shmcabinet_mgr_destroy(shmcabinet_mgr_tp smc_mgr);

/* allocates a new payload slot in shared memory and populates shared memory
 * connection information in a shm_item. the item is unlocked when returned.
 * shm_items must be freed with a call to shmcabinet_mgr_free.
 * returns NULL if there was an error, otherwise a poginter to a new shm_mem_item
 * containing the newly allocated cabinet slot information.
 */
shm_item_tp shmcabinet_mgr_alloc(shmcabinet_mgr_tp smc_mgr);

/* opens an exisiting payload slot in shared memory and populates shared memory
 * connection information in a shm_item. the item is unlocked when returned.
 * shm_items must be freed with a call to shmcabinet_mgr_free.
 * returns NULL if there was an error, otherwise a poginter to a new shm_mem_item
 * containing the existing cabinet slot information.
 */
shm_item_tp shmcabinet_mgr_open(shmcabinet_mgr_tp smc_mgr, shmcabinet_info_tp shm_info, guint32 slot_id);

/* frees a previously created shm_item by deallocating its shared memory cabinet
 * slot. shared memory may or may not be unmapped, following the unmap policy
 * described above.
 */
void shmcabinet_mgr_free(shmcabinet_mgr_tp smc_mgr, shm_item_tp shm_item);

/* obtains a read-lock on the shared memory associated with the given item.
 * the item must be unlocked with shmcabinet_mgr_readunlock before further
 * write operations on the item or payload can successfully complete. for
 * deadlock avoidance, read-locks are not allowed on items that already have a
 * write-lock.
 * returns 1 if the lock was successful, 0 otherwise.
 */
guint8 shmcabinet_mgr_readlock(shm_item_tp item);

/* unlocks a read-lock on the shared memory associated with the given item.
 * there is no effect for items that do not possess a read-lock.
 * returns 1 if the unlock was successful, 0 otherwise.
 */
guint8 shmcabinet_mgr_readunlock(shm_item_tp item);

/* obtains a write-lock on the shared memory associated with the given item.
 * the item must be unlocked with shmcabinet_mgr_writeunlock before further
 * read or write operations on the item or payload can successfully complete.
 * for deadlock avoidance, write-locks are not allowed on items that already
 * have a read-lock.
 * returns 1 if the lock was successful, 0 otherwise.
 */
guint8 shmcabinet_mgr_writelock(shm_item_tp item);

/* unlocks a write-lock on the shared memory associated with the given item.
 * there is no effect for items that do not possess a write-lock.
 * returns 1 if the unlock was successful, 0 otherwise.
 */
guint8 shmcabinet_mgr_writeunlock(shm_item_tp item);

/* callbacks for destroying hashtables. not meant to be called. */
void shmcabinet_mgr_shm_item_destroy_cb(gpointer value, gint key);
void shmcabinet_mgr_shm_destroy_cb(gpointer value, gint key);

#endif /* SHMCABINET_MGR_H_ */
