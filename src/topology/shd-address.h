/*
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

#ifndef SHD_ADDRESS_H_
#define SHD_ADDRESS_H_

#include "shadow.h"

/*
 * host order INADDR_LOOPBACK: 2130706433, INADDR_ANY: 0, INADDR_NONE: 4294967295, INADDR_BROADCAST: 4294967295
 * network order INADDR_LOOPBACK: 16777343, INADDR_ANY: 0, INADDR_NONE: 4294967295, INADDR_BROADCAST: 4294967295
 */

typedef struct _Address Address;

struct _Address {
	/* IP must be first so we can cast an Address to an in_addr_t */
	guint32 ip;
	gchar* ipString;
	gchar* name;
	MAGIC_DECLARE;
};

Address* address_new(guint32 ip, const gchar* name);
void address_free(gpointer data);

gboolean address_isEqual(Address* a, Address* b);
guint32 address_toHostIP(Address* address);
gchar* address_toHostIPString(Address* address);
guint32 address_toNetworkIP(Address* address);
gchar* address_toHostName(Address* address);


#endif /* SHD_ADDRESS_H_ */
