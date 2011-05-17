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

#include <dlfcn.h>
#include <time.h>
#include <stddef.h>

#include "preload.h"
#include "vsystem_intercept.h"
#include "global.h"

#define SYSTEM_LIB_PREFIX "intercept_"

/* Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in DVN since we expect the locations of the
 * functions to be the same for all nodes.
 */
typedef time_t (*time_fp)(time_t*);
typedef int (*clock_gettime_fp)(clockid_t, struct timespec *);
typedef int (*gethostname_fp)(char*, size_t);
typedef int (*getaddrinfo_fp)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
typedef int (*freeaddrinfo_fp)(struct addrinfo*);

/* save pointers to dvn vsystem functions */
static time_fp _vsystem_time = NULL;
static clock_gettime_fp _vsystem_clock_gettime = NULL;
static gethostname_fp _vsystem_gethostname = NULL;
static getaddrinfo_fp _vsystem_getaddrinfo = NULL;
static freeaddrinfo_fp _vsystem_freeaddrinfo = NULL;

static clock_gettime_fp _clock_gettime = NULL;

time_t time(time_t *t)  {
	/* this call is never forwarded to time() */
	time_fp* fp_ptr = &_vsystem_time;
	char* f_name = SYSTEM_LIB_PREFIX "time";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(t);
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
	clock_gettime_fp* fp_ptr = &_vsystem_clock_gettime;
	char* f_name = SYSTEM_LIB_PREFIX "clock_gettime";

	if(clk_id != CLOCK_REALTIME) {
		fp_ptr = &_clock_gettime;
		f_name = "clock_gettime";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(clk_id, tp);
}

int gethostname(char* name, size_t len) {
	/* this call is never forwarded to gethostname() */
	gethostname_fp* fp_ptr = &_vsystem_gethostname;
	char* f_name = SYSTEM_LIB_PREFIX "gethostname";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(name, len);
}

int getaddrinfo(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res) {
	/* this call is never forwarded to getaddrinfo() */
	getaddrinfo_fp* fp_ptr = &_vsystem_getaddrinfo;
	char* f_name = SYSTEM_LIB_PREFIX "getaddrinfo";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(node, service, hints, res);
}

void freeaddrinfo(struct addrinfo *res) {
	/* this call is never forwarded to freeaddrinfo() */
	freeaddrinfo_fp* fp_ptr = &_vsystem_freeaddrinfo;
	char* f_name = SYSTEM_LIB_PREFIX "freeaddrinfo";

	/* third arg is nothing since we return void */
	PRELOAD_LOOKUP(fp_ptr, f_name, );
	(*fp_ptr)(res);
}
