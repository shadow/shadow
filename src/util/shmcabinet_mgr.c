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

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "shmcabinet_mgr.h"
#include "shmcabinet.h"
#include "rwlock_mgr.h"
#include "hash.h"
#include "log.h"
#include "utility.h"

static void shmcabinet_mgr_lazy_create(shmcabinet_mgr_tp smc_mgr);
static void shmcabinet_mgr_shm_item_destroy(shm_item_tp shm_item);
static void shmcabinet_mgr_shm_destroy(shm_tp shm);
static uint32_t shmcabinet_mgr_allocatable_slots(shmcabinet_mgr_tp smc_mgr);

shmcabinet_mgr_tp shmcabinet_mgr_create(size_t payload_size, uint32_t payloads_per_cabinet, uint32_t unmap_threshold,
		enum rwlock_mgr_type cabinet_lock_type, enum rwlock_mgr_type slot_lock_type) {
	shmcabinet_mgr_tp smc_mgr = malloc(sizeof(shmcabinet_mgr_t));

	smc_mgr->payload_size = payload_size;
	smc_mgr->payloads_per_cabinet = payloads_per_cabinet;
	smc_mgr->min_payloads_threshold = unmap_threshold * payloads_per_cabinet;
	smc_mgr->cabinet_lock_type = cabinet_lock_type;
	smc_mgr->slot_lock_type = slot_lock_type;

	/* we will lazy create our data structures in case shmem is never used */
	smc_mgr->shm_owned_available = NULL;
	smc_mgr->shm_owned = NULL;
	smc_mgr->shm_unowned = NULL;

	return smc_mgr;
}

static void shmcabinet_mgr_lazy_create(shmcabinet_mgr_tp smc_mgr) {
	if(smc_mgr != NULL) {
		if(smc_mgr->shm_owned == NULL) {
			smc_mgr->shm_owned = g_hash_table_new(g_int_hash, g_int_equal);
		}
		if(smc_mgr->shm_unowned == NULL) {
			smc_mgr->shm_unowned = g_hash_table_new(g_int_hash, g_int_equal);
		}
		if(smc_mgr->shm_owned_available == NULL) {
			smc_mgr->shm_owned_available = g_queue_new();
		}
	}
}

void shmcabinet_mgr_destroy(shmcabinet_mgr_tp smc_mgr) {
	if(smc_mgr != NULL) {
		/* detach from our referenced shm cabinets */
		if(smc_mgr->shm_owned != NULL) {
			g_hash_table_foreach(smc_mgr->shm_owned, (GHFunc)shmcabinet_mgr_shm_destroy_cb, NULL);
			g_hash_table_destroy(smc_mgr->shm_owned);
		}

		/* detach from other shm cabinets we mapped to our address space */
		if(smc_mgr->shm_unowned != NULL) {
			g_hash_table_foreach(smc_mgr->shm_unowned, (GHFunc)shmcabinet_mgr_shm_destroy_cb, NULL);
			g_hash_table_destroy(smc_mgr->shm_unowned);
		}

		/* already destroyed the shm, so just destroy the list */
		if(smc_mgr->shm_owned_available != NULL) {
			g_queue_free(smc_mgr->shm_owned_available);
		}

		free(smc_mgr);
	}
}

void shmcabinet_mgr_shm_item_destroy_cb(void* value, int key) {
	shmcabinet_mgr_shm_item_destroy((shm_item_tp) value);
}

static void shmcabinet_mgr_shm_item_destroy(shm_item_tp shm_item) {
	if(shm_item != NULL && shm_item->shm != NULL) {
		/* close the slot, it will be deallocated automatically */
		if(shmcabinet_close(shm_item->shm->cabinet, shm_item->payload) != SHMCABINET_SUCCESS) {
			dlogf(LOG_ERR, "shmcabinet_mgr_shm_item_destroy: problem closing shm item\n");
		}
		free(shm_item);
	}
}

void shmcabinet_mgr_shm_destroy_cb(void* value, int key) {
	shmcabinet_mgr_shm_destroy((shm_tp) value);
}

static void shmcabinet_mgr_shm_destroy(shm_tp shm) {
	if(shm != NULL) {
		/* unmap the cabinet, it will destroy itself automatically */
		if(shmcabinet_unmap(shm->cabinet) != SHMCABINET_SUCCESS) {
			dlogf(LOG_ERR, "shmcabinet_mgr_shm_destroy: problem unmapping shm\n");
		}
		free(shm);
	}
}

shm_item_tp shmcabinet_mgr_alloc(shmcabinet_mgr_tp smc_mgr) {
	if(smc_mgr != NULL) {
		/* we are using shm, make sure our data structs exist */
		shmcabinet_mgr_lazy_create(smc_mgr);

		/* get available shm */
		shm_tp shm = NULL;
		if(smc_mgr->shm_owned_available != NULL && g_queue_get_length(smc_mgr->shm_owned_available) > 0) {
			shm = g_queue_peek_head(smc_mgr->shm_owned_available);

			if(shm != NULL && shm->cabinet != NULL && shmcabinet_slots_available(shm->cabinet) == 0) {
				dlogf(LOG_WARN, "shmcabinet_mgr_alloc: shm cabinet is full, I will try to correct\n");
				g_queue_pop_head(smc_mgr->shm_owned_available);
				shm = NULL;
			}
		}

		if(shm == NULL) {
			/* we need more memory */
			shmcabinet_tp cabinet = shmcabinet_create(smc_mgr->payloads_per_cabinet, smc_mgr->payload_size,
					smc_mgr->cabinet_lock_type, smc_mgr->slot_lock_type);
			if(cabinet != NULL) {
				/* create a new shm to hold cabinet and info */
				shm = malloc(sizeof(shm_t));
				shm->cabinet = cabinet;
				shmcabinet_get_info(cabinet, &shm->info);
				shm->references = 1;
				shm->owned = 1;

				/* keep track of the new shm */
				g_queue_push_tail(smc_mgr->shm_owned_available, shm);
				g_hash_table_insert(smc_mgr->shm_owned, int_key(shm->info.cabinet_id), shm);
			} else {
				dlogf(LOG_ERR, "shmcabinet_mgr_alloc: problem creating new shared memory cabinet\n");
				return NULL;
			}
		} else {
			/* use exisiting shm */
			(shm->references)++;
		}

		/* make sure we can get a payload spot */
		void* payload = shmcabinet_allocate(shm->cabinet);
		if(payload == NULL) {
			dlogf(LOG_ERR, "shmcabinet_mgr_alloc: problem allocating payload in cabinet\n");
			return NULL;
		}

		/* after allocation, check space and update available list */
		if(shmcabinet_slots_available(shm->cabinet) == 0) {
			g_queue_remove(smc_mgr->shm_owned_available, shm);
		}

		/* we're all set to return the item */
		shm_item_tp item = malloc(sizeof(shm_item_t));
		item->shm = shm;
		item->payload = payload;
		item->slot_id = shmcabinet_get_id(shm->cabinet, payload);
		item->num_readlocks = 0;
		item->num_writelocks = 0;

		return item;
	}

	return NULL;
}

shm_item_tp shmcabinet_mgr_open(shmcabinet_mgr_tp smc_mgr, shmcabinet_info_tp shm_info, uint32_t slot_id) {
	if(smc_mgr != NULL) {
		/* we are using shm, make sure our data structs exist */
		shmcabinet_mgr_lazy_create(smc_mgr);

		unsigned int key = twouint_hash(shm_info->process_id, shm_info->cabinet_id);

		/* check if we are already connected to this shm */
		shm_tp shm = g_hash_table_lookup(smc_mgr->shm_unowned, &key);

		if(shm == NULL) {
			/* we need to map the given cabinet */
			shmcabinet_tp cabinet = shmcabinet_map(shm_info->process_id, shm_info->cabinet_id, shm_info->cabinet_size);

			if(cabinet != NULL) {
				shm = malloc(sizeof(shm_t));
				shm->cabinet = cabinet;
				shm->info = *shm_info;
				shm->references = 1;
				shm->owned = 0;

				/* keep track of the mapped shm */
				g_hash_table_insert(smc_mgr->shm_unowned, int_key(key), shm);
			} else {
				dlogf(LOG_ERR, "shmcabinet_mgr_open: problem mapping shared memory cabinet\n");
				return NULL;
			}
		} else {
			/* use exisiting shm */
			(shm->references)++;
		}

		/* make sure we can open the payload spot */
		void* payload = shmcabinet_open(shm->cabinet, slot_id);
		if(payload == NULL) {
			dlogf(LOG_ERR, "shmcabinet_mgr_open: problem allocating payload in cabinet\n");
			return NULL;
		}

		/* we're all set to return the item */
		shm_item_tp item = malloc(sizeof(shm_item_t));
		item->shm = shm;
		item->payload = payload;
		item->slot_id = shmcabinet_get_id(shm->cabinet, payload);
		item->num_readlocks = 0;
		item->num_writelocks = 0;

		return item;
	}

	return NULL;
}

void shmcabinet_mgr_free(shmcabinet_mgr_tp smc_mgr, shm_item_tp item) {
	if(smc_mgr != NULL && item != NULL && item->shm != NULL) {
		shm_tp shm = item->shm;

		/* we are finished with the payload, free cabinet memory */
		if(shmcabinet_close(shm->cabinet, item->payload) != SHMCABINET_SUCCESS) {
			dlogf(LOG_ERR, "shmcabinet_mgr_free: problem closing payload in cabinet\n");
			return;
		}

		(shm->references)--;

		/* unmapping policy depends on ownership */
		if(shm->owned) {
			/* if closing that payload made the cabinet unfull, update avail list */
			if(shmcabinet_slots_available(shm->cabinet) == 1) {
				g_queue_push_tail(smc_mgr->shm_owned_available, shm);
			}

			if(shm->references == 0) {
				/* unmap the cabinet if we stay above the threshold */
				uint32_t avail_payloads = shmcabinet_mgr_allocatable_slots(smc_mgr);
				if(avail_payloads - smc_mgr->payloads_per_cabinet >= smc_mgr->min_payloads_threshold) {
					if(shmcabinet_unmap(shm->cabinet) == SHMCABINET_SUCCESS) {
						g_queue_remove(smc_mgr->shm_owned_available, shm);
						g_hash_table_remove(smc_mgr->shm_owned, &shm->info.cabinet_id);
						free(shm);
					} else {
						dlogf(LOG_ERR, "shmcabinet_mgr_free: problem unmapping owned cabinet\n");
					}
				}
			}
		} else {
			/* if no references remain, unmap immediately */
			if(shm->references == 0) {
				unsigned int key = twouint_hash(shm->info.process_id, shm->info.cabinet_id);
				if(shmcabinet_unmap(shm->cabinet) == SHMCABINET_SUCCESS) {
					g_hash_table_remove(smc_mgr->shm_unowned, &key);
					free(shm);
				} else {
					dlogf(LOG_ERR, "shmcabinet_mgr_free: problem unmapping unowned cabinet\n");
				}
			}
		}

		free(item);
	}
}

static uint32_t shmcabinet_mgr_allocatable_slots(shmcabinet_mgr_tp smc_mgr) {
	if(smc_mgr != NULL) {
		/* iterate the avail list and count the free slots in each cabinet */
		uint32_t allocatable = 0;

		GList* iter = g_queue_peek_head_link(smc_mgr->shm_owned_available);
		while(iter != NULL) {
			shm_tp shm = iter->data;
			if(shm != NULL) {
				allocatable += shmcabinet_slots_available(shm->cabinet);
			}
                        iter = iter->next;
		}

		return allocatable;
	}
	return 0;
}

uint8_t shmcabinet_mgr_readlock(shm_item_tp item) {
	if(item != NULL && item->shm != NULL) {
		/* avoid deadlocks: no read-locks allowed if we already hold a write-lock */
		if(item->num_writelocks < 1) {
			if(shmcabinet_readlock(item->shm->cabinet, item->payload) == SHMCABINET_SUCCESS) {
				(item->num_readlocks)++;
				return 1;
			} else {
				dlogf(LOG_ERR, "shmcabinet_mgr_readlock: error locking item for reads\n");
			}
		}
	}
	return 0;
}

uint8_t shmcabinet_mgr_readunlock(shm_item_tp item) {
	if(item != NULL && item->shm != NULL) {
		/* dont unlock if we dont have a lock */
		if(item->num_readlocks > 0) {
			if(shmcabinet_readunlock(item->shm->cabinet, item->payload) == SHMCABINET_SUCCESS) {
				(item->num_readlocks)--;
				return 1;
			} else {
				dlogf(LOG_ERR, "shmcabinet_mgr_readunlock: error unlocking item for reads\n");
			}
		}
	}
	return 0;
}

uint8_t shmcabinet_mgr_writelock(shm_item_tp item) {
	if(item != NULL && item->shm != NULL) {
		/* avoid deadlocks: no write-locks allowed if we already hold a read-lock */
		if(item->num_readlocks < 1) {
			if(shmcabinet_writelock(item->shm->cabinet, item->payload) == SHMCABINET_SUCCESS) {
				(item->num_writelocks)++;
				return 1;
			} else {
				dlogf(LOG_ERR, "shmcabinet_mgr_writelock: error locking item for writes\n");
			}
		}
	}
	return 0;
}

uint8_t shmcabinet_mgr_writeunlock(shm_item_tp item) {
	if(item != NULL && item->shm != NULL) {
		/* dont unlock if we dont have a lock */
		if(item->num_writelocks > 0) {
			if(shmcabinet_writeunlock(item->shm->cabinet, item->payload) == SHMCABINET_SUCCESS) {
				(item->num_writelocks)--;
				return 1;
			} else {
				dlogf(LOG_ERR, "shmcabinet_mgr_writeunlock: error unlocking item for writes\n");
			}
		}
	}
	return 0;
}
