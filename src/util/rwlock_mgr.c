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

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>

#include "rwlock_mgr.h"
#include "rwlock.h"

rwlock_mgr_tp rwlock_mgr_create(enum rwlock_mgr_type type, guint8 is_process_shared) {
	ssize_t lmgr_size = rwlock_mgr_sizeof(type);
	if(lmgr_size < 0) {
		return NULL;
	}

	rwlock_mgr_tp lmgr = malloc(lmgr_size);

	if(rwlock_mgr_init(lmgr, type, is_process_shared) == RWLOCK_MGR_SUCCESS) {
		return lmgr;
	} else {
		free(lmgr);
		return NULL;
	}
}

enum rwlock_mgr_status rwlock_mgr_destroy(rwlock_mgr_tp lmgr) {
	if(lmgr != NULL) {
		gint retval = rwlock_mgr_uninit(lmgr);
		free(lmgr);
		return retval;;
	} else {
		return RWLOCK_MGR_ERR_INVALID_MGR;
	}
}

enum rwlock_mgr_status rwlock_mgr_init(rwlock_mgr_tp lmgr, enum rwlock_mgr_type type, guint8 is_process_shared) {
	if(lmgr != NULL) {

		switch (type) {
			case RWLOCK_MGR_TYPE_CUSTOM: {
				if(rwlock_init((rwlock_tp)(lmgr->lock), is_process_shared) != RWLOCK_SUCCESS) {
					goto err;
				}
				break;
			}

			case RWLOCK_MGR_TYPE_PTHREAD: {
				pthread_rwlockattr_t attr;
				if(pthread_rwlockattr_init(&attr) != 0 ||
						pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
					goto err;
				}

				if(pthread_rwlock_init((pthread_rwlock_t*)(lmgr->lock), &attr) != 0) {
					pthread_rwlockattr_destroy(&attr);
					goto err;
				}

				if(pthread_rwlockattr_destroy(&attr) != 0) {
					goto err;
				}
				break;
			}

			case RWLOCK_MGR_TYPE_SEMAPHORE: {
				if(sem_init((sem_t*)(lmgr->lock), is_process_shared, 1) != 0) {
					goto err;
				}
				break;
			}

			default: {
				return RWLOCK_MGR_ERR_INVALID_TYPE;
			}
		}

		lmgr->type = type;

		return RWLOCK_MGR_SUCCESS;
	} else {
		return RWLOCK_MGR_ERR_INVALID_MGR;
	}

err:
	return RWLOCK_MGR_ERROR;
}

enum rwlock_mgr_status rwlock_mgr_uninit(rwlock_mgr_tp lmgr) {
	if(lmgr != NULL) {

		switch (lmgr->type) {
			case RWLOCK_MGR_TYPE_CUSTOM: {
				if(rwlock_destroy((rwlock_tp)(lmgr->lock)) != RWLOCK_SUCCESS) {
					goto err;
				}
				break;
			}

			case RWLOCK_MGR_TYPE_PTHREAD: {
				if(pthread_rwlock_destroy((pthread_rwlock_t*)(lmgr->lock)) != 0) {
					goto err;
				}
				break;
			}

			case RWLOCK_MGR_TYPE_SEMAPHORE: {
				if(sem_destroy((sem_t*)(lmgr->lock)) != 0) {
					goto err;
				}
				break;
			}

			default: {
				return RWLOCK_MGR_ERR_INVALID_TYPE;
			}
		}

		lmgr->type = 0;

		return RWLOCK_MGR_SUCCESS;
	} else {
		return RWLOCK_MGR_ERR_INVALID_MGR;
	}

err:
	return RWLOCK_MGR_ERROR;
}

ssize_t rwlock_mgr_sizeof(enum rwlock_mgr_type type) {
	ssize_t size = sizeof(rwlock_mgr_t);

	switch (type) {
		case RWLOCK_MGR_TYPE_CUSTOM: {
			size += sizeof(rwlock_t);
			break;
		}

		case RWLOCK_MGR_TYPE_PTHREAD: {
			size += sizeof(pthread_rwlock_t);
			break;
		}

		case RWLOCK_MGR_TYPE_SEMAPHORE: {
			size += sizeof(sem_t);
			break;
		}

		default: {
			size = -1;
			break;
		}
	}

	return size;
}

enum rwlock_mgr_status rwlock_mgr_lockcontrol(rwlock_mgr_tp lmgr, enum rwlock_mgr_command command) {
	if(lmgr != NULL) {
		switch (lmgr->type) {
			case RWLOCK_MGR_TYPE_CUSTOM: {
				switch (command) {
					case RWLOCK_MGR_COMMAND_READLOCK: {
						if(rwlock_readlock((rwlock_tp)(lmgr->lock)) != RWLOCK_SUCCESS) {
							goto err;
						}
						break;
					}

					case RWLOCK_MGR_COMMAND_READUNLOCK: {
						if(rwlock_readunlock((rwlock_tp)(lmgr->lock)) != RWLOCK_SUCCESS) {
							goto err;
						}
						break;
					}

					case RWLOCK_MGR_COMMAND_WRITELOCK: {
						if(rwlock_writelock((rwlock_tp)(lmgr->lock)) != RWLOCK_SUCCESS) {
							goto err;
						}
						break;
					}

					case RWLOCK_MGR_COMMAND_WRITEUNLOCK: {
						if(rwlock_writeunlock((rwlock_tp)(lmgr->lock)) != RWLOCK_SUCCESS) {
							goto err;
						}
						break;
					}

					default: {
						return RWLOCK_MGR_ERR_INVALID_COMMAND;
					}
				}
				break;
			}

			case RWLOCK_MGR_TYPE_PTHREAD: {
				switch (command) {
					case RWLOCK_MGR_COMMAND_READLOCK: {
						if(pthread_rwlock_rdlock((pthread_rwlock_t*)(lmgr->lock)) != 0) {
							goto err;
						}
						break;
					}

					case RWLOCK_MGR_COMMAND_WRITELOCK: {
						if(pthread_rwlock_wrlock((pthread_rwlock_t*)(lmgr->lock)) != 0) {
							goto err;
						}
						break;
					}

					/* XXX unlocking an unlocked lock causes errors with POSIX rwlock */
					case RWLOCK_MGR_COMMAND_READUNLOCK:
					case RWLOCK_MGR_COMMAND_WRITEUNLOCK: {
						if(pthread_rwlock_unlock((pthread_rwlock_t*)(lmgr->lock)) != 0) {
							goto err;
						}
						break;
					}

					default: {
						return RWLOCK_MGR_ERR_INVALID_COMMAND;
					}
				}
				break;
			}

			case RWLOCK_MGR_TYPE_SEMAPHORE: {
				switch (command) {
					case RWLOCK_MGR_COMMAND_READLOCK:
					case RWLOCK_MGR_COMMAND_WRITELOCK: {
						if(sem_wait((sem_t*)(lmgr->lock)) != 0) {
							goto err;
						}
						break;
					}

					case RWLOCK_MGR_COMMAND_READUNLOCK:
					case RWLOCK_MGR_COMMAND_WRITEUNLOCK: {
						/* dont increment sem if its not locked */
						gint lockval = 0;
						if(sem_getvalue((sem_t*)(lmgr->lock), &lockval) != 0) {
							goto err;
						}

						if(lockval > 0) {
							/* sem is not locked */
							break;
						}

						if(sem_post((sem_t*)(lmgr->lock)) != 0) {
							goto err;
						}
						break;
					}

					default: {
						return RWLOCK_MGR_ERR_INVALID_COMMAND;
					}
				}
				break;
			}

			default: {
				return RWLOCK_MGR_ERR_INVALID_TYPE;
			}
		}

		return RWLOCK_MGR_SUCCESS;
	} else {
		return RWLOCK_MGR_ERR_INVALID_MGR;
	}

err:
	return RWLOCK_MGR_ERROR;
}
