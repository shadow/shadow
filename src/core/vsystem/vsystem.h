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

#ifndef VSYSTEM_H_
#define VSYSTEM_H_

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

time_t vsystem_time(time_t* t);
int vsystem_clock_gettime(clockid_t clk_id, struct timespec *tp);
int vsystem_gethostname(char *name, size_t len);
int vsystem_getaddrinfo(char *node, const char *service,
		const struct addrinfo *hints, struct addrinfo **res);
void vsystem_freeaddrinfo(struct addrinfo *res);
void vsystem_add_cpu_load(double number_of_encryptions);

#endif /* VSYSTEM_H_ */
