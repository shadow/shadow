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
#include <pthread.h>
#include <errno.h>

#include "rwlock.h"

/* TODO refactor */

gint rwlock_init(rwlock_tp lock, gint is_process_shared) {
	if(lock != NULL) {
		gint result = 0;
		pthread_mutexattr_t mattr;
		pthread_condattr_t cattr;
		pthread_mutexattr_t* mattribute = NULL;
		pthread_condattr_t* cattribute = NULL;

		if(is_process_shared) {
			mattribute = &mattr;

			result = pthread_mutexattr_init(mattribute);
			if(result != 0) {
				errno = result;
				return RWLOCK_ERROR;
			}

			result = pthread_mutexattr_setpshared(mattribute, PTHREAD_PROCESS_SHARED);
			if(result != 0) {
				errno = result;
				return RWLOCK_ERROR;
			}

			cattribute = &cattr;

			result = pthread_condattr_init(cattribute);
			if(result != 0) {
				errno = result;
				return RWLOCK_ERROR;
			}

			result = pthread_condattr_setpshared(cattribute, PTHREAD_PROCESS_SHARED);
			if(result != 0) {
				errno = result;
				return RWLOCK_ERROR;
			}
		}

		result = pthread_cond_init(&lock->read_condition, cattribute);
		if(result != 0) {
			errno = result;
			return RWLOCK_ERROR;
		}

		result = pthread_cond_init(&lock->write_condition, cattribute);
		if(result != 0) {
			pthread_cond_destroy(&lock->read_condition);
			errno = result;
			return RWLOCK_ERROR;
		}

		result = pthread_mutex_init(&lock->mutex, mattribute);
		if(result != 0) {
			pthread_cond_destroy(&lock->read_condition);
			pthread_cond_destroy(&lock->write_condition);
			errno = result;
			return RWLOCK_ERROR;
		}

		lock->readers_active = 0;
		lock->writers_active = 0;
		lock->readers_waiting = 0;
		lock->writers_waiting = 0;
		lock->valid = RWLOCK_READY;
		return RWLOCK_SUCCESS;
	} else {
		errno = EINVAL;
		return RWLOCK_ERROR;
	}
}

gint rwlock_destroy(rwlock_tp lock) {
	if(lock != NULL && lock->valid == RWLOCK_READY) {
		gint result1 = 0, result2 = 0, result3 = 0;

		result1 = pthread_mutex_lock(&lock->mutex);
		if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		}

		if(lock->readers_active > 0 || lock->writers_active > 0 ||
				lock->readers_waiting > 0 || lock->writers_waiting > 0) {
			pthread_mutex_unlock(&lock->mutex);
			errno = EBUSY;
			return RWLOCK_ERROR;
		}

		lock->valid = 0;
		result1 = pthread_mutex_unlock(&lock->mutex);
		result2 = pthread_cond_destroy(&lock->read_condition);
		result3 = pthread_cond_destroy(&lock->write_condition);

		if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		} else if (result2 != 0) {
			errno = result2;
			return RWLOCK_ERROR;
		} else if (result3 != 0) {
			errno = result3;
			return RWLOCK_ERROR;
		} else {
			return RWLOCK_SUCCESS;
		}
	} else {
		errno = EINVAL;
		return RWLOCK_ERROR;
	}
}

static void rwlock_read_cleanup(gpointer parameter) {
	rwlock_tp lock = (rwlock_tp) parameter;
	(lock->readers_waiting)--;
	pthread_mutex_unlock(&lock->mutex);
}

gint rwlock_readlock(rwlock_tp lock) {
	if(lock != NULL && lock->valid == RWLOCK_READY) {
		gint result1 = 0, result2 = 0;

		result1 = pthread_mutex_lock(&lock->mutex);
		if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		}

		/* we have the mutex, if theres a writer, we have to block */
		if(lock->writers_active > 0) {
			(lock->readers_waiting)++;
			pthread_cleanup_push(&rwlock_read_cleanup, (gpointer ) lock);

			/* keep waiting until writers are done */
			while(lock->writers_active > 0) {
				result2 = pthread_cond_wait(&lock->read_condition, &lock->mutex);
				if(result2 != 0) {
					break;
				}
			}

			/* pop but dont execute */
			pthread_cleanup_pop(0);
			(lock->readers_waiting)--;
		}

		/* either no one is active, or we have successful condition */
		if(result2 == 0) {
			(lock->readers_active)++;
		}

		result1 = pthread_mutex_unlock(&lock->mutex);
		if(result2 != 0) {
			errno = result2;
			return RWLOCK_ERROR;
		} else if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		} else {
			return RWLOCK_SUCCESS;
		}
	} else {
		errno = EINVAL;
		return RWLOCK_ERROR;
	}
}

gint rwlock_readunlock(rwlock_tp lock) {
	if(lock != NULL && lock->valid == RWLOCK_READY) {
		gint result1 = 0, result2 = 0;

		result1 = pthread_mutex_lock(&lock->mutex);
		if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		}

		if(lock->readers_active > 0) {
			(lock->readers_active)--;

			if(lock->readers_active == 0 && lock->writers_active == 0) {
				/* someone can start */
				result2 = pthread_cond_signal(&lock->write_condition);
			}
		}

		result1 = pthread_mutex_unlock(&lock->mutex);
		if(result2 != 0) {
			errno = result2;
			return RWLOCK_ERROR;
		} else if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		} else {
			return RWLOCK_SUCCESS;
		}
	} else {
		errno = EINVAL;
		return RWLOCK_ERROR;
	}
}

static void rwlock_write_cleanup(gpointer parameter) {
	rwlock_tp lock = (rwlock_tp) parameter;
	(lock->writers_waiting)--;
	pthread_mutex_unlock(&lock->mutex);
}

gint rwlock_writelock(rwlock_tp lock) {
	if(lock != NULL && lock->valid == RWLOCK_READY) {
		gint result1 = 0, result2 = 0;

		result1 = pthread_mutex_lock(&lock->mutex);
		if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		}

		if(lock->writers_active > 0 || lock->readers_active > 0) {
			(lock->writers_waiting)++;
			pthread_cleanup_push(&rwlock_write_cleanup, (gpointer ) lock);

			while(lock->writers_active > 0 || lock->readers_active > 0) {
				result2 = pthread_cond_wait(&lock->write_condition, &lock->mutex);
				if(result2 != 0) {
					break;
				}
			}

			pthread_cleanup_pop(0);
			(lock->writers_waiting)--;
		}

		/* either no one is active, or we have successful condition */
		if(result2 == 0) {
			(lock->writers_active)++;
		}

		result1 = pthread_mutex_unlock(&lock->mutex);
		if(result2 != 0) {
			errno = result2;
			return RWLOCK_ERROR;
		} else if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		} else {
			return RWLOCK_SUCCESS;
		}
	} else {
		errno = EINVAL;
		return RWLOCK_ERROR;
	}
}

gint rwlock_writeunlock(rwlock_tp lock) {
	if(lock != NULL && lock->valid == RWLOCK_READY) {
		gint result1 = 0, result2 = 0;

		result1 = pthread_mutex_lock(&lock->mutex);
		if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		}

		if(lock->writers_active > 0) {
			(lock->writers_active)--;

			if(lock->writers_waiting > 0) {
				/* tell the next writer to start */
				result2 = pthread_cond_signal(&lock->write_condition);
			} else if(lock->readers_waiting > 0) {
				/* all waiting readers can start */
				result2 = pthread_cond_broadcast(&lock->read_condition);
			}
		}

		result1 = pthread_mutex_unlock(&lock->mutex);
		if(result2 != 0) {
			errno = result2;
			return RWLOCK_ERROR;
		} else if(result1 != 0) {
			errno = result1;
			return RWLOCK_ERROR;
		} else {
			return RWLOCK_SUCCESS;
		}
	} else {
		errno = EINVAL;
		return RWLOCK_ERROR;
	}
}


