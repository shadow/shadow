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

#include "shadow.h"

struct _EpollDescriptor {
	Descriptor super;

	/* other members specific to epoll */

	MAGIC_DECLARE;
};

/* should only be called from descriptor dereferencing the functionTable */
static void epoll_free(gpointer data) {
	EpollDescriptor* epoll = data;
	MAGIC_ASSERT(epoll);

	MAGIC_CLEAR(epoll);
	g_free(epoll);
}

DescriptorFunctionTable epollFunctions = {
	(DescriptorFreeFunc) epoll_free,
	MAGIC_VALUE
};

EpollDescriptor* epoll_new(gint handle) {
	g_assert(handle >= VNETWORK_MIN_SD);
	EpollDescriptor* epoll = g_new0(EpollDescriptor, 1);
	MAGIC_INIT(epoll);

	descriptor_init(&(epoll->super), DT_EPOLL, &epollFunctions, handle);

	/* allocate backend needed for managing events for this descriptor */

	return epoll;
}

gint epoll_control(EpollDescriptor* epoll, gint operation, gint fileDescriptor,
		struct epoll_event* event) {
	MAGIC_ASSERT(epoll);

	switch (operation) {
		case EPOLL_CTL_ADD: {
			//	EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor fd is already registered with this epoll instance.
			break;
		}

		case EPOLL_CTL_MOD: {
			//	ENOENT op was EPOLL_CTL_MOD or EPOLL_CTL_DEL, and fd is not registered with this epoll instance.
			break;
		}

		case EPOLL_CTL_DEL: {
			//	ENOENT op was EPOLL_CTL_MOD or EPOLL_CTL_DEL, and fd is not registered with this epoll instance.
			break;
		}

		default: {

			break;
		}
	}

	return -1;
}

gint epoll_getEvents(EpollDescriptor* epoll, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents) {
	MAGIC_ASSERT(epoll);

//	The memory area pointed to by events will contain the events
//    that will be available for the caller.  Up to maxevents are returned by epoll_wait().

//    The  data  of  each  returned  structure will contain the same data the user set with
//    an epoll_ctl(2) (EPOLL_CTL_ADD,EPOLL_CTL_MOD) while the events member will contain the
//    returned event bit field.

	return -1;
}
