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

#ifndef SHD_UTILITY_H_
#define SHD_UTILITY_H_

#include "shadow.h"

guint utility_ipPortHash(in_addr_t ip, in_port_t port);
guint utility_int16Hash(gconstpointer value);
gboolean utility_int16Equal(gconstpointer value1, gconstpointer value2);

#endif /* SHD_UTILITY_H_ */
