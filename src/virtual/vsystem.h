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

#ifndef VSYSTEM_H_
#define VSYSTEM_H_

#include <glib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

time_t vsystem_time(time_t* t);
gint vsystem_clock_gettime(clockid_t clk_id, struct timespec *tp);
gint vsystem_gethostname(gchar *name, size_t len);
gint vsystem_getaddrinfo(gchar *node, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res);
void vsystem_freeaddrinfo(struct addrinfo *res);
void vsystem_add_cpu_load(gdouble number_of_encryptions);

#endif /* VSYSTEM_H_ */
