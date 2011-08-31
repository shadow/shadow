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

#ifndef RESOLVER_H_
#define RESOLVER_H_

#include <glib.h>
#include <stdint.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>


/* should cover all digits in UINT32_MAX */
#define RESOLVER_ID_MAXLENSTR 12

typedef struct resolver_entry_s {
	guint32 KBps_down;
	guint32 KBps_up;
	in_addr_t addr;
	gchar hostname[];
} resolver_entry_t, *resolver_entry_tp;

typedef struct resolver_s {
	guint32 unique_id_counter;
	GHashTable *name_entry;
	GHashTable *addr_entry;
	gint pid;
} resolver_t, *resolver_tp;

resolver_tp resolver_create(gint process_id);
void resolver_destroy(resolver_tp resolver);
void resolver_destroy_cb(gint key, gpointer value, gpointer param); 

void resolver_add(resolver_tp r, gchar* name, in_addr_t addr, guint8 prepend_unique_id, guint32 KBps_down, guint32 KBps_up);
void resolver_remove_byname(resolver_tp r, gchar* name);
void resolver_remove_byaddr(resolver_tp r, in_addr_t addr);
in_addr_t* resolver_resolve_byname(resolver_tp r, gchar* name);
gchar* resolver_resolve_byaddr(resolver_tp r, in_addr_t addr);

guint32 resolver_get_minbw(resolver_tp r, in_addr_t addr);
guint32 resolver_get_upbw(resolver_tp r, in_addr_t addr);
guint32 resolver_get_downbw(resolver_tp r, in_addr_t addr);

#endif /* RESOLVER_H_ */
