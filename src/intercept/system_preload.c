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

#include <dlfcn.h>
#include <time.h>
#include <stddef.h>

#include "shadow.h"

#define SYSTEM_LIB_PREFIX "intercept_"

/* Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in Shadow since we expect the locations of the
 * functions to be the same for all nodes.
 */
typedef time_t (*time_fp)(time_t*);
typedef int (*clock_gettime_fp)(clockid_t, struct timespec *);
typedef int (*gethostname_fp)(char*, size_t);
typedef int (*getaddrinfo_fp)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
typedef int (*freeaddrinfo_fp)(struct addrinfo*);

/* save pointers to shadow vsystem functions */
static time_fp _vsystem_time = NULL;
static clock_gettime_fp _vsystem_clock_gettime = NULL;
static gethostname_fp _vsystem_gethostname = NULL;
static getaddrinfo_fp _vsystem_getaddrinfo = NULL;
static freeaddrinfo_fp _vsystem_freeaddrinfo = NULL;

/* real system functions */
static time_fp _time = NULL;
static clock_gettime_fp _clock_gettime = NULL;
static gethostname_fp _gethostname = NULL;
static getaddrinfo_fp _getaddrinfo = NULL;
static freeaddrinfo_fp _freeaddrinfo = NULL;

time_t time(time_t *t)  {
	time_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "time", _time, SYSTEM_LIB_PREFIX, _vsystem_time, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(t);
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
	clock_gettime_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "clock_gettime", _clock_gettime, SYSTEM_LIB_PREFIX, _vsystem_clock_gettime, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(clk_id, tp);
}

int gethostname(char* name, size_t len) {
	gethostname_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostname", _gethostname, SYSTEM_LIB_PREFIX, _vsystem_gethostname, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(name, len);
}

int getaddrinfo(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res) {
	getaddrinfo_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getaddrinfo", _getaddrinfo, SYSTEM_LIB_PREFIX, _vsystem_getaddrinfo, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(node, service, hints, res);
}

void freeaddrinfo(struct addrinfo *res) {
	freeaddrinfo_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "freeaddrinfo", _freeaddrinfo, SYSTEM_LIB_PREFIX, _vsystem_freeaddrinfo, 1);
	/* third arg is nothing since we return void */
	PRELOAD_LOOKUP(func, funcName,);
	(*func)(res);
}
