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

#ifndef _pipecloud_h
#define _pipecloud_h

#include <sys/sem.h>
#include <sys/ipc.h>
#include <mqueue.h>
#include "list.h"
#include "ipcsync.h"

#define PIPECLOUD_CHUNK_DATA_SIZE 1024
#define PIPECLOUD_SEM_MUTEX 0
#define PIPECLOUD_SEM_READCOND 1
#define PIPECLOUD_SEM_WRITECOND 2
#define PIPECLOUD_MAX_SIZE 64

#define PIPECLOUD_MODE_BLOCK 1
#define PIPECLOUD_MODE_POLL 0

#define PIPECLOUD_TIMEOUT_SEC 0
#define PIPECLOUD_TIMEOUT_NSEC 10000000

typedef struct pipecloud_buffer_t {
	size_t len;
	size_t offset;
	char data[];
} pipecloud_buffer_t, * pipecloud_buffer_tp;

typedef struct pipecloud_t {
	/* total number of mailbox endpoints */
	unsigned int num_pipes;

	mqd_t * mqs;

	size_t max_msg_size;

	/* used for "localized" (e.g. this-process-owned) data */
	struct {
		/* what process are we? */
		int id;

		/* waiting input queue of pipecloud_buffer_t objects */
		list_tp in;

		/* total quantity of waiting in data */
		size_t waiting_in;
	} localized;
} pipecloud_t, * pipecloud_tp;

pipecloud_tp pipecloud_create(unsigned int attendees, size_t size, unsigned int num_wakeup_channels);

void pipecloud_destroy(pipecloud_tp);
void pipecloud_config_localized(pipecloud_tp pipecloud, unsigned int id);
int pipecloud_get_wakeup_fd(pipecloud_tp pc);

void pipecloud_select(pipecloud_tp pipecloud, int block);

size_t pipecloud_write(pipecloud_tp pipecloud, unsigned int dest, char * data, size_t data_size);
size_t pipecloud_write_core(pipecloud_tp pipecloud, unsigned int dest, char * data, size_t data_size);

int pipecloud_read(pipecloud_tp pipecloud, char * buffer, size_t size);
int pipecloud_peek(pipecloud_tp pipecloud, char * buffer, size_t size);

void pipecloud_localize_reads(pipecloud_tp pipecloud);

#endif

