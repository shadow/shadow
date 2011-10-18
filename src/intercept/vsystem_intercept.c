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

#include <glib.h>
#include <time.h>
#include <stddef.h>
#include <sys/socket.h>

#include "shadow.h"

time_t intercept_time(time_t* t) {
	INTERCEPT_CONTEXT_SWITCH(,
			time_t r = vsystem_time(t),
			return r);
}

gint intercept_clock_gettime(clockid_t clk_id, struct timespec *tp) {
	INTERCEPT_CONTEXT_SWITCH(,
			gint r = vsystem_clock_gettime(clk_id, tp),
			return r);
}

gint intercept_gethostname(gchar *name, size_t len) {
	INTERCEPT_CONTEXT_SWITCH(,
			gint r = vsystem_gethostname(name, len),
			return r);
}

gint intercept_getaddrinfo(gchar *node, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res) {
	INTERCEPT_CONTEXT_SWITCH(,
			gint r = vsystem_getaddrinfo(node, service, hgints, res),
			return r);
}

void intercept_freeaddrinfo(struct addrinfo *res) {
	vsystem_freeaddrinfo(res);
	INTERCEPT_CONTEXT_SWITCH(,
			vsystem_freeaddrinfo(res),);
}
