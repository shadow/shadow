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

#ifndef RESOLVER_H_
#define RESOLVER_H_

#include <stdint.h>
#include <netinet/in.h>

#include "hashtable.h"

/* should cover all digits in UINT32_MAX */
#define RESOLVER_ID_MAXLENSTR 12

typedef struct resolver_entry_s {
	uint32_t KBps_down;
	uint32_t KBps_up;
	in_addr_t addr;
	char hostname[];
} resolver_entry_t, *resolver_entry_tp;

typedef struct resolver_s {
	uint32_t unique_id_counter;
	hashtable_tp name_entry;
	hashtable_tp addr_entry;
	int pid;
} resolver_t, *resolver_tp;

resolver_tp resolver_create(int process_id);
void resolver_destroy(resolver_tp resolver);
void resolver_destroy_cb(void* value, int key);

void resolver_add(resolver_tp r, char* name, in_addr_t addr, uint8_t prepend_unique_id, uint32_t KBps_down, uint32_t KBps_up);
void resolver_remove_byname(resolver_tp r, char* name);
void resolver_remove_byaddr(resolver_tp r, in_addr_t addr);
in_addr_t* resolver_resolve_byname(resolver_tp r, char* name);
char* resolver_resolve_byaddr(resolver_tp r, in_addr_t addr);

uint32_t resolver_get_minbw(resolver_tp r, in_addr_t addr);
uint32_t resolver_get_upbw(resolver_tp r, in_addr_t addr);
uint32_t resolver_get_downbw(resolver_tp r, in_addr_t addr);

#endif /* RESOLVER_H_ */
