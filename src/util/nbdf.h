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

#ifndef _nbdf_h
#define _nbdf_h

#include <glib.h>
#include "pipecloud.h"
#include "socket.h"

#define NBDF_DEFAULT_AVAILABLE 64

typedef struct nbdf_t {
	gchar * data;
	guint avail;
	guint consumed;
} nbdf_t, *nbdf_tp;

nbdf_tp nbdf_construct(gchar * format, ...);
nbdf_tp nbdf_import_frame(socket_tp s);
nbdf_tp nbdf_import_frame_pipecloud(pipecloud_tp);
gint nbdf_frame_avail(socket_tp s);
void nbdf_send(nbdf_tp nb, socket_tp s);
void nbdf_send_pipecloud(nbdf_tp nb, gint destination_mbox, pipecloud_tp pc);
void nbdf_read(nbdf_tp nb, gchar * format, ...);
void nbdf_free(nbdf_tp nbdf);
nbdf_tp nbdf_dup(nbdf_tp nb);

#endif /*NBDF_H_*/
