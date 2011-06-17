/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#include "vsocket_mgr.h"
#include "vpipe.h"
#include "hashtable.h"
#include "log.h"

static enum vpipe_status vpipe_unid_destroy(vpipe_unid_tp unipipe) {
	if(unipipe != NULL) {
		linkedbuffer_destroy(unipipe->buffer);
		free(unipipe);
		return VPIPE_DESTROYED;
	}
	return VPIPE_FAILURE;
}

static vpipe_unid_tp vpipe_unid_create(vevent_mgr_tp vev_mgr, vpipe_id read_fd, vpipe_id write_fd, in_addr_t addr) {
	vpipe_unid_tp unipipe = malloc(sizeof(vpipe_unid_t));

	unipipe->buffer = linkedbuffer_create(8096);
	unipipe->read_fd = read_fd;
	unipipe->write_fd = write_fd;
	unipipe->flags = 0;

	if(unipipe->buffer != NULL) {
		return unipipe;
	} else {
		vpipe_unid_destroy(unipipe);
		return NULL;
	}
}

static size_t vpipe_unid_read(vpipe_unid_tp unipipe, vpipe_id fd, void* dst, size_t num_bytes) {
	if(unipipe != NULL) {
		if(fd == unipipe->read_fd && !(unipipe->flags & VPIPE_READER_CLOSED)) {
			return linkedbuffer_read(unipipe->buffer, dst, num_bytes);
		} else {
			dlogf(LOG_ERR, "vpipe_unid_read: fd %u not allowed to read\n", fd);
		}
	}
	return VPIPE_IO_ERROR;
}

static size_t vpipe_unid_write(vpipe_unid_tp unipipe, vpipe_id fd, const void* src, size_t num_bytes) {
	if(unipipe != NULL) {
		if(fd == unipipe->write_fd && !(unipipe->flags & VPIPE_WRITER_CLOSED)) {
			return linkedbuffer_write(unipipe->buffer, src, num_bytes);
		} else {
			dlogf(LOG_ERR, "vpipe_unid_write: fd %u not allowed to write\n", fd);
		}
	}
	return VPIPE_IO_ERROR;
}

static enum vpipe_status vpipe_unid_close(vpipe_unid_tp unipipe, vpipe_id fd) {
	if(unipipe != NULL) {
		if(fd == unipipe->read_fd || fd == unipipe->write_fd) {
			if(fd == unipipe->read_fd) {
				unipipe->flags |= VPIPE_READER_CLOSED;
			} else {
				unipipe->flags |= VPIPE_WRITER_CLOSED;
			}

			if((unipipe->flags & VPIPE_READER_CLOSED) && (unipipe->flags & VPIPE_WRITER_CLOSED)) {
				return vpipe_unid_destroy(unipipe);
			}
			return VPIPE_CLOSED;
		} else {
			dlogf(LOG_ERR, "vpipe_unid_close: invalid pipe fd %u\n", fd);
		}
	}
	return VPIPE_FAILURE;
}

static enum vpipe_status vpipe_bid_destroy(vpipe_bid_tp bipipe) {
	if(bipipe != NULL) {
		vpipe_unid_destroy(bipipe->pipea);
		vpipe_unid_destroy(bipipe->pipeb);
		vepoll_destroy(bipipe->vepolla);
		vepoll_destroy(bipipe->vepollb);
		free(bipipe);
		return VPIPE_DESTROYED;
	}
	return VPIPE_FAILURE;
}

static vpipe_bid_tp vpipe_bid_create(vevent_mgr_tp vev_mgr, vpipe_id fda, vpipe_id fdb, in_addr_t addr) {
	vpipe_bid_tp bipipe = malloc(sizeof(vpipe_bid_t));

	bipipe->fda = fda;
	bipipe->fdb = fdb;

	/* fda reads from pipea and writes to pipeb */
	bipipe->pipea = vpipe_unid_create(vev_mgr, fda, fdb, addr);
	/* fdb reads from pipeb and writes to pipea */
	bipipe->pipeb = vpipe_unid_create(vev_mgr, fdb, fda, addr);

	/* watch status of each fd */
	bipipe->vepolla = vepoll_create(vev_mgr, addr, fda);
	bipipe->vepollb = vepoll_create(vev_mgr, addr, fdb);

	if(bipipe->pipea != NULL && bipipe->pipeb != NULL && bipipe->vepolla != NULL && bipipe->vepollb != NULL) {
		/* pipes are always active and available for writing */
		vepoll_mark_active(bipipe->vepolla);
		vepoll_mark_active(bipipe->vepollb);
		vepoll_mark_available(bipipe->vepolla, VEPOLL_WRITE);
		vepoll_mark_available(bipipe->vepollb, VEPOLL_WRITE);
		return bipipe;
	} else {
		vpipe_bid_destroy(bipipe);
		return NULL;
	}
}

static ssize_t vpipe_bid_read(vpipe_bid_tp bipipe, vpipe_id fd, void* dst, size_t num_bytes) {
	if(bipipe != NULL) {
		if(fd == bipipe->fda) {
			/* fda tries to read from pipea */
			ssize_t s = vpipe_unid_read(bipipe->pipea, fd, dst, num_bytes);
			if(s <= 0) {
				/* fda can no longer read */
				vepoll_mark_unavailable(bipipe->vepolla, VEPOLL_READ);
				vepoll_mark_available(bipipe->vepollb, VEPOLL_WRITE);
				/* now if fdb (writer for pipea) already closed, we return EOF */
				if(bipipe->pipea->flags & VPIPE_WRITER_CLOSED) {
					return 0;
				}
			} else {
				return s;
			}
		} else if(fd == bipipe->fdb) {
			ssize_t s = vpipe_unid_read(bipipe->pipeb, fd, dst, num_bytes);
			if(s <= 0) {
				vepoll_mark_unavailable(bipipe->vepollb, VEPOLL_READ);
				vepoll_mark_available(bipipe->vepolla, VEPOLL_WRITE);
				/* now if fda (writer for pipeb) already closed, we return EOF */
				if(bipipe->pipeb->flags & VPIPE_WRITER_CLOSED) {
					return 0;
				}
			} else {
				return s;
			}
		} else {
			dlogf(LOG_ERR, "vpipe_bid_read: fd %u not allowed to read\n", fd);
		}
	}
	return VPIPE_IO_ERROR;
}

static ssize_t vpipe_bid_write(vpipe_bid_tp bipipe, vpipe_id fd, const void* src, size_t num_bytes) {
	if(bipipe != NULL) {
		if(fd == bipipe->fda) {
			/* fda tries to write to pipeb
			 * if fdb (reader for pipeb) already closed, we return EOF */
			if(bipipe->pipeb->flags & VPIPE_READER_CLOSED) {
				return 0;
			}
			ssize_t s = vpipe_unid_write(bipipe->pipeb, fd, src, num_bytes);
			if(s <= 0) {
				/* fda can no longer write */
				vepoll_mark_unavailable(bipipe->vepolla, VEPOLL_WRITE);
			} else {
				/* fdb can now read what fda just wrote */
				vepoll_mark_available(bipipe->vepollb, VEPOLL_READ);
				return s;
			}
		} else if(fd == bipipe->fdb) {
			/* fdb tries to write to pipea
			 * if fda (reader for pipea) already closed, we return EOF */
			if(bipipe->pipea->flags & VPIPE_READER_CLOSED) {
				return 0;
			}
			ssize_t s = vpipe_unid_write(bipipe->pipea, fd, src, num_bytes);
			if(s <= 0) {
				vepoll_mark_unavailable(bipipe->vepollb, VEPOLL_WRITE);
			} else {
				vepoll_mark_available(bipipe->vepolla, VEPOLL_READ);
				return s;
			}
		} else {
			dlogf(LOG_ERR, "vpipe_bid_write: fd %u not allowed to write\n", fd);
		}
	}
	return VPIPE_IO_ERROR;
}

static enum vpipe_status vpipe_bid_close(vpipe_bid_tp bipipe, vpipe_id fd) {
	if(bipipe != NULL) {
		/* this fd refers to an end of both pipes */
		if(vpipe_unid_close(bipipe->pipea, fd) == VPIPE_DESTROYED) {
			bipipe->pipea = NULL;
		}
		if(vpipe_unid_close(bipipe->pipeb, fd) == VPIPE_DESTROYED) {
			bipipe->pipeb = NULL;
		}

		if(fd == bipipe->fda) {
			vepoll_mark_inactive(bipipe->vepolla);
		}
		if(fd == bipipe->fdb) {
			vepoll_mark_inactive(bipipe->vepollb);
		}

		if(bipipe->pipea == NULL && bipipe->pipeb == NULL) {
			return vpipe_bid_destroy(bipipe);
		} else {
			return VPIPE_CLOSED;
		}
	}
	return VPIPE_FAILURE;
}

static void vpipe_destroy_cb(void* value, int key) {
	/* the lower level close functions dont modify the hashtable, so we should
	 * be safe using them. they should take care not to double-free. */
	vpipe_bid_close(value, key);
}

vpipe_mgr_tp vpipe_mgr_create(in_addr_t addr) {
	vpipe_mgr_tp mgr = malloc(sizeof(vpipe_mgr_t));
	mgr->bipipes = hashtable_create(10, 0.90);
	mgr->addr = addr;
	return mgr;
}

void vpipe_mgr_destroy(vpipe_mgr_tp mgr) {
	if(mgr != NULL) {
		hashtable_walk(mgr->bipipes, &vpipe_destroy_cb);
		hashtable_destroy(mgr->bipipes);
		free(mgr);
	}
}

enum vpipe_status vpipe_create(vevent_mgr_tp vev_mgr, vpipe_mgr_tp mgr, vpipe_id fda, vpipe_id fdb) {
	if(mgr != NULL) {
		vpipe_bid_tp bipipe = vpipe_bid_create(vev_mgr, fda, fdb, mgr->addr);

		if(bipipe != NULL) {
			/* TODO: check for collisions */
			hashtable_set(mgr->bipipes, fda, bipipe);
			hashtable_set(mgr->bipipes, fdb, bipipe);
			return VPIPE_SUCCESS;
		}
	}
	return VPIPE_FAILURE;
}

ssize_t vpipe_read(vpipe_mgr_tp mgr, vpipe_id fd, void* dst, size_t num_bytes) {
	if(mgr != NULL) {
		vpipe_bid_tp bipipe = hashtable_get(mgr->bipipes, fd);
		return vpipe_bid_read(bipipe, fd, dst, num_bytes);
	}
	return VPIPE_IO_ERROR;
}

ssize_t vpipe_write(vpipe_mgr_tp mgr, vpipe_id fd, const void* src, size_t num_bytes) {
	if(mgr != NULL) {
		vpipe_bid_tp bipipe = hashtable_get(mgr->bipipes, fd);
		return vpipe_bid_write(bipipe, fd, src, num_bytes);
	}
	return VPIPE_IO_ERROR;
}

enum vpipe_status vpipe_close(vpipe_mgr_tp mgr, vpipe_id fd) {
	if(mgr != NULL) {
		/* consider it closed if its not mapped */
		vpipe_bid_tp vp = hashtable_remove(mgr->bipipes, fd);

		return vpipe_bid_close(vp, fd);
	}
	return VPIPE_FAILURE;
}

enum vpipe_status vpipe_stat(vpipe_mgr_tp mgr, vpipe_id fd) {
	if(mgr != NULL) {
		vpipe_bid_tp vp = hashtable_get(mgr->bipipes, fd);
		if(vp != NULL) {
			/* since the pipe exists, we know this fd hasn't closed yet. so it
			 * can still read. but we need to check that the other end didn't
			 * close, closing our write end and forcing us into readonly mode.
			 */
			vpipe_unid_tp writerpipe = NULL;

			if(vp->pipea != NULL && fd == vp->pipea->write_fd) {
				writerpipe = vp->pipea;
			} else if(vp->pipeb != NULL && fd == vp->pipeb->write_fd) {
				writerpipe = vp->pipeb;
			} else {
				dlogf(LOG_ERR, "vpipe_stat: fd %u not a writer for either end of pipe!?\n", fd);
				return VPIPE_FAILURE;
			}

			/* check if we can still write */
			if(writerpipe->flags & VPIPE_READER_CLOSED) {
				return VPIPE_READONLY;
			} else {
				return VPIPE_OPEN;
			}
		} else {
			/* fd is not in the table, so fd either closed or is not a vpipe */
			return VPIPE_CLOSED;
		}
	}
	return VPIPE_FAILURE;
}

vepoll_tp vpipe_get_poll(vpipe_mgr_tp mgr, vpipe_id fd) {
	if(mgr != NULL) {
		vpipe_bid_tp vp = hashtable_get(mgr->bipipes, fd);
		if(vp != NULL) {
			if(fd == vp->fda) {
				return vp->vepolla;
			} else if(fd == vp->fdb) {
				return vp->vepollb;
			}
		}
	}
	return NULL;
}
