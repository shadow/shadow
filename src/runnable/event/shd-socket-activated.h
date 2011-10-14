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

#ifndef SHD_SOCKET_ACTIVATED_H_
#define SHD_SOCKET_ACTIVATED_H_

#include "shadow.h"

typedef struct _SocketActivatedEvent SocketActivatedEvent;

struct _SocketActivatedEvent {
	Event super;
	guint16 socketDescriptor;
	MAGIC_DECLARE;
};

SocketActivatedEvent* socketactivated_new(guint16 socketDescriptor);
void socketactivated_run(SocketActivatedEvent* event, Node* node);
void socketactivated_free(SocketActivatedEvent* event);

#endif /* SHD_SOCKET_ACTIVATED_H_ */
