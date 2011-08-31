/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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
#include <time.h>
#include <stddef.h>
#include <sys/socket.h>

#include "vsystem_intercept.h"
#include "vsystem.h"

time_t intercept_time(time_t* t) {
	return vsystem_time(t);
}

gint intercept_clock_gettime(clockid_t clk_id, struct timespec *tp) {
	return vsystem_clock_gettime(clk_id, tp);
}

gint intercept_gethostname(gchar *name, size_t len) {
	return vsystem_gethostname(name, len);
}

gint intercept_getaddrinfo(gchar *node, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res) {
	return vsystem_getaddrinfo(node, service, hgints, res);
}

void intercept_freeaddrinfo(struct addrinfo *res) {
	vsystem_freeaddrinfo(res);
}
