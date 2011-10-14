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

#ifndef SHD_TCP_DACK_TIMER_EXPIRED_H_
#define SHD_TCP_DACK_TIMER_EXPIRED_H_

#include "shadow.h"

typedef struct _TCPDAckTimerExpiredEvent TCPDAckTimerExpiredEvent;

struct _TCPDAckTimerExpiredEvent {
	Event super;
	guint16 socketDescriptor;
	MAGIC_DECLARE;
};

TCPDAckTimerExpiredEvent* tcpdacktimerexpired_new(guint16 socketDescriptor);
void tcpdacktimerexpired_run(TCPDAckTimerExpiredEvent* event, Node* node);
void tcpdacktimerexpired_free(TCPDAckTimerExpiredEvent* event);

#endif /* SHD_TCP_DACK_TIMER_EXPIRED_H_ */
