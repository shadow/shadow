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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include "shmcabinet_internal.h"
#include "shmcabinet.h"
#include "rwlock_mgr.h"

/* process-global cabinet id counter */
static uint32_t next_cabinet_id = 0;

static shmcabinet_tp shmcabinet_map_helper(uint32_t process_id, uint32_t cabinet_id,
		size_t cabinet_size, int flags);
static uint32_t shmcabinet_lockop(shmcabinet_tp cabinet, void* payload, enum shmcabinet_lockop op);

shmcabinet_tp shmcabinet_create(uint32_t num_slots, size_t slot_payload_size,
		enum rwlock_mgr_type cabinet_lock_type, enum rwlock_mgr_type slot_lock_type) {
	if(num_slots > SHMCABINET_MAX_SLOTS) {
		/* they asked for too many slots */
		errno = EINVAL;
		goto err;
	} else {
		/* compute size for memory mapping */
		size_t slot_header_size = sizeof(shmcabinet_slot_t);
		ssize_t slot_lock_size = rwlock_mgr_sizeof(slot_lock_type);
		if(slot_lock_size == -1) {
			goto err;
		}
		size_t slot_total_size = slot_header_size + slot_lock_size + slot_payload_size;

		size_t cabinet_header_size = sizeof(shmcabinet_t);
		ssize_t cabinet_lock_size = rwlock_mgr_sizeof(cabinet_lock_type);
		if(cabinet_lock_size == -1) {
			goto err;
		}
		size_t cabinet_total_size = cabinet_header_size + cabinet_lock_size + (num_slots * slot_total_size);

		uint32_t process_id = getpid();

		/* TODO: need lock if using multiple threads */
		uint32_t cabinet_id = next_cabinet_id++;

		shmcabinet_tp cabinet = shmcabinet_map_helper(process_id, cabinet_id,
				cabinet_total_size, O_RDWR | O_CREAT | O_TRUNC);

		if(cabinet == NULL) {
			errno = ENOMEM;
			goto err;
		} else {
			/* initialize the cabinet. sizes are needed for macros called below. */
			cabinet->pid = process_id;
			cabinet->id = cabinet_id;
			cabinet->size = cabinet_total_size;
			cabinet->num_opened = 1;
			cabinet->slot_size = slot_total_size;
			cabinet->num_slots = num_slots;
			cabinet->num_slots_allocated = 0;
			cabinet->cabinet_lock_size = (size_t)cabinet_lock_size;
			cabinet->slot_lock_size = (size_t)slot_lock_size;
			cabinet->head_slot_offset = shmcabinet_ID_TO_OFFSET(cabinet, 0);

			/* unlocked, process-shared lock for the cabinet */
			if(rwlock_mgr_init(shmcabinet_CABINET_TO_LOCK(cabinet), cabinet_lock_type, 1) != RWLOCK_MGR_SUCCESS) {
				goto err;
			}

			/* each slot also has a lock */
			shmcabinet_slot_tp slot = shmcabinet_ID_TO_SLOT(cabinet, 0);
			for(int i = 0; i < num_slots; i++) {
				/* unlocked, process-shared lock for the slot */
				if(rwlock_mgr_init(shmcabinet_SLOT_TO_LOCK(slot), slot_lock_type, 1) != RWLOCK_MGR_SUCCESS) {
					goto err;
				}

				if(i == num_slots - 1) {
					slot->next_slot_offset = SHMCABINET_INVALID;
				} else {
					slot->next_slot_offset = shmcabinet_ID_TO_OFFSET(cabinet, i+1);
				}

				slot->id = i;
				slot->num_opened = 0;
				slot->valid = SHMCABINET_VALID;

				slot = shmcabinet_ID_TO_SLOT(cabinet, i+1);
			}

			cabinet->valid = SHMCABINET_VALID;

			return cabinet;
		}
	}

err:
	perror("shmcabinet_create");
	return NULL;
}

shmcabinet_tp shmcabinet_map(uint32_t process_id, uint32_t cabinet_id, size_t cabinet_size) {
	shmcabinet_tp cabinet = shmcabinet_map_helper(process_id, cabinet_id, cabinet_size, O_RDWR | O_EXCL);

	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID) {
		if(rwlock_mgr_writelock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
			goto err;
		}

		(cabinet->num_opened)++;

		if(rwlock_mgr_writeunlock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
			goto err;
		}

		return cabinet;
	} else {
		errno = EINVAL;
		return NULL;
	}

err:
	perror("shmcabinet_map");
	shmcabinet_unmap(cabinet);
	return NULL;
}

static shmcabinet_tp shmcabinet_map_helper(uint32_t process_id, uint32_t cabinet_id,
		size_t cabinet_size, int flags) {
	/* convert ids to the correct shmem name */
	shmcabinet_NAME(namebuf, process_id, cabinet_id);

	/* get the shared memory fd from /dev/shm */
	int shmem_fd = shm_open(namebuf, flags, 0600);
	if(shmem_fd == -1) {
		perror("shmcabinet_map_helper");
		return NULL;
	}

	/* if we are creating, we need to truncate the file */
	if(flags & O_CREAT) {
		if(ftruncate(shmem_fd, cabinet_size) == -1) {
			perror("shmcabinet_map_helper");
			shm_unlink(namebuf);
			close(shmem_fd);
			return NULL;
		}
	}

	/* map it to our address space */
	shmcabinet_tp cabinet = (shmcabinet_tp) mmap(NULL, cabinet_size,
			PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0);
	if(cabinet == (void*) -1) {
		perror("shmcabinet_map_helper");
		shm_unlink(namebuf);
		close(shmem_fd);
		return NULL;
	}

	/* TODO is it ok to close here? what exactly does close do for shmem?
	 * Closing does not affect the mapping, and later attempts to sync (write)
	 * the mapping still succeed. This is our last chance to close as we
	 * don't [currently] store the file descriptor.
	 */
	if(close(shmem_fd) == -1) {
		perror("shmcabinet_map_helper");
	}
	return cabinet;
}

uint32_t shmcabinet_unmap(shmcabinet_tp cabinet) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID) {
		if(rwlock_mgr_writelock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
			goto err;
		}
		
		(cabinet->num_opened)--;
		if(cabinet->num_opened == 0) {
			cabinet->valid = SHMCABINET_INVALID;
		}

		if(rwlock_mgr_writeunlock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
			goto err;
		}

		if(cabinet->num_opened == 0 && cabinet->valid == SHMCABINET_INVALID) {
			/* i was the last process using this cabinet, destroy it */
			shmcabinet_slot_tp slot = shmcabinet_ID_TO_SLOT(cabinet, 0);

			/* destroy all the slots */
			for(int i = 0; i < cabinet->num_slots; i++) {
				if(slot != NULL && slot->valid == SHMCABINET_VALID) {
					slot->valid = SHMCABINET_INVALID;
					slot->next_slot_offset = SHMCABINET_INVALID;
					if(rwlock_mgr_uninit(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
						goto err;
					}
				} else {
					errno = EFAULT;
					goto err;
				}
				slot = shmcabinet_ID_TO_SLOT(cabinet, i+1);
			}

			shmcabinet_NAME(namebuf, cabinet->pid, cabinet->id);
			if(rwlock_mgr_uninit(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS ||
					munmap(cabinet, cabinet->size) == -1 ||
					shm_unlink(namebuf) == -1) {
				goto err;
			}
		} else {
			/* others still have the cabinet mapped. lets hope they unmap,
			 * or we will have a zombie segment in /dev/shm/.
			 */
			if(munmap(cabinet, cabinet->size) == -1){
				goto err;
			}
		}

		return SHMCABINET_SUCCESS;
	} else {
		errno = EINVAL;
		goto err;
	}
	return SHMCABINET_ERROR;
err:
	perror("shmcabinet_unmap");
	return SHMCABINET_ERROR;
}

void* shmcabinet_allocate(shmcabinet_tp cabinet) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID) {
		if(rwlock_mgr_writelock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
			goto err;
		}

		/* check if we have an available slot. here we could also check:
		 *   cabinet->head_slot_offset < cabinet->size
		 *   cabinet->head_slot_offset != SHMCABINET_INVALID
		 */
		if(cabinet->num_slots_allocated < cabinet->num_slots) {
			/* no need to lock the slot, its not allocated */
			shmcabinet_slot_tp slot = shmcabinet_HEAD(cabinet);

			if(slot != NULL && slot->valid == SHMCABINET_VALID) {
				/* we are essentially opening the slot here, but do not call shmcabinet_open
				 * to avoid the extra lock and unlock operations.
				 * update tracking info. */
				cabinet->head_slot_offset = slot->next_slot_offset;
				(cabinet->num_slots_allocated)++;
				slot->next_slot_offset = SHMCABINET_INVALID;
				(slot->num_opened)++;

				if(rwlock_mgr_writeunlock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
					goto err;
				}

				return shmcabinet_SLOT_TO_PAYLOAD(cabinet, slot);
			} else {
				rwlock_mgr_writeunlock(shmcabinet_CABINET_TO_LOCK(cabinet));
				errno = EINVAL;
				goto err;
			}
		} else {
			rwlock_mgr_writeunlock(shmcabinet_CABINET_TO_LOCK(cabinet));
			errno = ENOMEM;
			goto err;
		}
	} else {
		errno = EINVAL;
		goto err;
	}

err:
	perror("shmcabinet_alloc");
	return NULL;
}

void* shmcabinet_open(shmcabinet_tp cabinet, uint32_t slot_id) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID && slot_id < cabinet->num_slots) {
		/* find the slot from the slot id */
		shmcabinet_slot_tp slot = shmcabinet_ID_TO_SLOT(cabinet, slot_id);

		if(slot != NULL && slot->valid == SHMCABINET_VALID) {
			if(rwlock_mgr_writelock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
				goto err;
			}

			(slot->num_opened)++;

			if(rwlock_mgr_writeunlock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
				goto err;
			}

			return shmcabinet_SLOT_TO_PAYLOAD(cabinet, slot);
		} else {
			errno = EINVAL;
			goto err;
		}
	} else {
		errno = EINVAL;
		goto err;
	}
	return NULL;
err:
	perror("shmcabinet_open");
	return NULL;
}

uint32_t shmcabinet_close(shmcabinet_tp cabinet, void* payload) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID) {
		/* get the slot */
		shmcabinet_slot_tp slot = shmcabinet_PAYLOAD_TO_SLOT(cabinet, payload);

		if(slot != NULL && slot->valid == SHMCABINET_VALID) {
			if(rwlock_mgr_writelock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
				goto err;
			}

			/* if the slot is allocated, next_slot_offset will be unset.
			 * we can't close an unallocated slot */
			if(slot->next_slot_offset == SHMCABINET_INVALID) {
				/* we are closing this slot */
				(slot->num_opened)--;
				
				if(slot->num_opened == 0) {
					/* deallocate slot */
					if(rwlock_mgr_writelock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
						rwlock_mgr_writeunlock(shmcabinet_SLOT_TO_LOCK(slot));
						goto err;
					}

					slot->next_slot_offset = cabinet->head_slot_offset;
					(cabinet->num_slots_allocated)--;
					cabinet->head_slot_offset = shmcabinet_ID_TO_OFFSET(cabinet, slot->id);

					if(rwlock_mgr_writeunlock(shmcabinet_CABINET_TO_LOCK(cabinet)) != RWLOCK_MGR_SUCCESS) {
						rwlock_mgr_writeunlock(shmcabinet_SLOT_TO_LOCK(slot));
						goto err;
					}
				}
			} else {
				/* cant close an unallocated slot */
				/* TODO find better errornum */
				rwlock_mgr_writeunlock(shmcabinet_SLOT_TO_LOCK(slot));
				errno = ENOENT;
				goto err;
			}
			
			if(rwlock_mgr_writeunlock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
				goto err;
			}

			return SHMCABINET_SUCCESS;
		} else {
			errno = EINVAL;
			goto err;
		}
	} else {
		errno = EINVAL;
		goto err;
	}

err:
	perror("shmcabinet_close");
	return SHMCABINET_ERROR;
}

static uint32_t shmcabinet_lockop(shmcabinet_tp cabinet, void* payload, enum shmcabinet_lockop op) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID && payload != NULL) {
		/* get the slot */
		shmcabinet_slot_tp slot = shmcabinet_PAYLOAD_TO_SLOT(cabinet, payload);

		if(slot != NULL && slot->valid == SHMCABINET_VALID) {
			switch (op) {
				case SHMCABINET_READLOCK:
				case SHMCABINET_WRITELOCK: {
					/* you may only operate on the slot if its allocated */
					if(op == SHMCABINET_READLOCK) {
						if(rwlock_mgr_readlock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
							goto err;
						}
					} else {
						if(rwlock_mgr_writelock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
							goto err;
						}
					}

					uint8_t is_allocated = slot->next_slot_offset == SHMCABINET_INVALID;
					if(!is_allocated) {
						/* it was not allocated, cancel the op */
						if(op == SHMCABINET_READLOCK) {
							if(rwlock_mgr_readunlock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
								goto err;
							}
						} else {
							if(rwlock_mgr_writeunlock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
								goto err;
							}
						}
						return SHMCABINET_ERROR;
					}

					return SHMCABINET_SUCCESS;
				}

				case SHMCABINET_READUNLOCK: {
					if(rwlock_mgr_readunlock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
						goto err;
					}

					return SHMCABINET_SUCCESS;
				}

				case SHMCABINET_WRITEUNLOCK: {
					if(rwlock_mgr_writeunlock(shmcabinet_SLOT_TO_LOCK(slot)) != RWLOCK_MGR_SUCCESS) {
						goto err;
					}

					return SHMCABINET_SUCCESS;
				}

				default: {
					return SHMCABINET_ERROR;
				}
			}
		}
	}
	return SHMCABINET_ERROR;
err:
	perror("shmcabinet_lockop");
	return SHMCABINET_ERROR;
}

uint32_t shmcabinet_readlock(shmcabinet_tp cabinet, void* payload) {
	return shmcabinet_lockop(cabinet, payload, SHMCABINET_READLOCK);
}

uint32_t shmcabinet_writelock(shmcabinet_tp cabinet, void* payload) {
	return shmcabinet_lockop(cabinet, payload, SHMCABINET_WRITELOCK);
}

uint32_t shmcabinet_readunlock(shmcabinet_tp cabinet, void* payload) {
	return shmcabinet_lockop(cabinet, payload, SHMCABINET_READUNLOCK);
}

uint32_t shmcabinet_writeunlock(shmcabinet_tp cabinet, void* payload) {
	return shmcabinet_lockop(cabinet, payload, SHMCABINET_WRITEUNLOCK);
}

uint32_t shmcabinet_get_info(shmcabinet_tp cabinet, shmcabinet_info_tp info) {
	if(cabinet != NULL && info != NULL) {
		info->cabinet_id = cabinet->id;
		info->cabinet_size = cabinet->size;
		info->process_id = cabinet->pid;
		return SHMCABINET_SUCCESS;
	}
	return SHMCABINET_ERROR;
}

uint32_t shmcabinet_get_id(shmcabinet_tp cabinet, void* payload) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID && payload != NULL) {
		shmcabinet_slot_tp slot = shmcabinet_PAYLOAD_TO_SLOT(cabinet, payload);

		/* only return id if the slot is allocated */
		if(slot != NULL && slot->valid == SHMCABINET_VALID) {
			if(rwlock_mgr_readlock(shmcabinet_SLOT_TO_LOCK(slot)) == RWLOCK_MGR_SUCCESS) {
				uint8_t bool = slot->next_slot_offset == SHMCABINET_INVALID;
				if(rwlock_mgr_readunlock(shmcabinet_SLOT_TO_LOCK(slot)) == RWLOCK_MGR_SUCCESS) {
					if(bool) {
						return slot->id;
					}
				}
			}
		}
	}
	return SHMCABINET_ERROR;
}

uint32_t shmcabinet_slots_available(shmcabinet_tp cabinet) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID) {
		if(rwlock_mgr_readlock(shmcabinet_CABINET_TO_LOCK(cabinet)) == RWLOCK_MGR_SUCCESS) {
			uint32_t num = cabinet->num_slots - cabinet->num_slots_allocated;
			if(rwlock_mgr_readunlock(shmcabinet_CABINET_TO_LOCK(cabinet)) == RWLOCK_MGR_SUCCESS) {
				return num;
			}
		}
	}
	return 0;
}

uint8_t shmcabinet_is_empty(shmcabinet_tp cabinet) {
	if(cabinet != NULL && cabinet->valid == SHMCABINET_VALID) {
		if(rwlock_mgr_readlock(shmcabinet_CABINET_TO_LOCK(cabinet)) == RWLOCK_MGR_SUCCESS) {
			uint8_t bool = cabinet->num_slots_allocated == 0;
			if(rwlock_mgr_readunlock(shmcabinet_CABINET_TO_LOCK(cabinet)) == RWLOCK_MGR_SUCCESS) {
				return bool;
			}
		}
	}
	return 0;
}
