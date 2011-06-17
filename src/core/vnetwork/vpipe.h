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

#ifndef VPIPE_H_
#define VPIPE_H_

#include <stdint.h>
#include <stddef.h>

#include "vepoll.h"
#include "linkedbuffer.h"
#include "hashtable.h"

typedef uint16_t vpipe_id;
#define VPIPE_IO_ERROR -1

enum vpipe_status {
	/* all positive status imply success */
	VPIPE_FAILURE=0, VPIPE_SUCCESS=1,
	VPIPE_CREATED=2, VPIPE_DESTROYED=4, VPIPE_OPEN=8, VPIPE_CLOSED=16, VPIPE_READONLY=32
};

enum vpipe_flags {
	VPIPE_READER_CLOSED=1, VPIPE_WRITER_CLOSED=2
};

typedef struct vpipe_unid_s {
	vpipe_id read_fd;
	vpipe_id write_fd;
	vepoll_tp read_poll;
	vepoll_tp write_poll;
	linkedbuffer_tp buffer;
	enum vpipe_flags flags;
} vpipe_unid_t, *vpipe_unid_tp;

typedef struct vpipe_bid_s {
	uint16_t fda;
	vpipe_unid_tp pipea;
	vepoll_tp vepolla;
	uint16_t fdb;
	vpipe_unid_tp pipeb;
	vepoll_tp vepollb;
} vpipe_bid_t, *vpipe_bid_tp;

typedef struct vpipe_mgr_s {
	hashtable_tp bipipes;
	in_addr_t addr;
} vpipe_mgr_t, *vpipe_mgr_tp;

vpipe_mgr_tp vpipe_mgr_create(in_addr_t addr);
void vpipe_mgr_destroy(vpipe_mgr_tp mgr);

enum vpipe_status vpipe_create(vevent_mgr_tp vev_mgr, vpipe_mgr_tp mgr, vpipe_id fda, vpipe_id fdb);
ssize_t vpipe_read(vpipe_mgr_tp mgr, vpipe_id fd, void* dst, size_t num_bytes);
ssize_t vpipe_write(vpipe_mgr_tp mgr, vpipe_id fd, const void* src, size_t num_bytes);
enum vpipe_status vpipe_close(vpipe_mgr_tp mgr, vpipe_id fd);
enum vpipe_status vpipe_stat(vpipe_mgr_tp mgr, vpipe_id fd);
vepoll_tp vpipe_get_poll(vpipe_mgr_tp mgr, vpipe_id fd);

#endif /* VPIPE_H_ */
