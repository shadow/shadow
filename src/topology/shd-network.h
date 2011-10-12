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

#ifndef SHD_NETWORK_H_
#define SHD_NETWORK_H_

#include "shadow.h"

/* FIXME: forward declaration to avoid circular dependencies... */
typedef struct _Link Link;

typedef struct _Network Network;

struct _Network {
	GQuark id;
	CumulativeDistribution* intranetLatency;
	/* links to other networks this network can access */
	GList* outgoingLinks;
	/* links from other networks that can access this network */
	GList* incomingLinks;
	/* map to outgoing links by network id */
	GHashTable* outgoingLinkMap;
	MAGIC_DECLARE;
};

Network* network_new(GQuark id, CumulativeDistribution* intranetLatency);
void network_free(gpointer data);
void network_addOutgoingLink(Network* network, Link* outgoingLink);
void network_addIncomingLink(Network* network, Link* incomingLink);
gint network_compare(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean network_equal(Network* a, Network* b);

#endif /* SHD_NETWORK_H_ */
