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

#include <glib.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stddef.h>

#include "shadow.h"

vpeer_tp vpeer_create(in_addr_t addr, in_port_t port){
	vpeer_tp peer = malloc(sizeof(vpeer_t));

	peer->addr = addr;
	peer->port = port;

	return peer;
}

void vpeer_destroy(vpeer_tp peer){
	if(peer != NULL){
		peer->addr = 0;
		peer->port = 0;

		free(peer);
	}
}
