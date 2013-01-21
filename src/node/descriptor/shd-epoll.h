/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#ifndef SHD_EPOLL_H_
#define SHD_EPOLL_H_

#include "shadow.h"

typedef struct _Epoll Epoll;

/* free this with descriptor_free() */
Epoll* epoll_new(gint handle);

gint epoll_control(Epoll* epoll, gint operation, Descriptor* descriptor,
		struct epoll_event* event);
gint epoll_controlOS(Epoll* epoll, gint operation, gint fileDescriptor,
		struct epoll_event* event);
gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents);

void epoll_descriptorStatusChanged(Epoll* epoll, Descriptor* descriptor);
void epoll_tryNotify(Epoll* epoll);

#endif /* SHD_EPOLL_H_ */
