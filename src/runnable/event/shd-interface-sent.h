/*
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

#ifndef SHD_INTERFACE_SENT_H_
#define SHD_INTERFACE_SENT_H_

#include "shadow.h"

typedef struct _InterfaceSentEvent InterfaceSentEvent;

InterfaceSentEvent* interfacesent_new(NetworkInterface* interface);
void interfacesent_run(InterfaceSentEvent* event, Node* node);
void interfacesent_free(InterfaceSentEvent* event);

#endif /* SHD_INTERFACE_SENT_H_ */
