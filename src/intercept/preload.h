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

#ifndef PRELOAD_H_
#define PRELOAD_H_

#include <stdio.h>
#include "log_codes.h"

/* handles for dlsym */
#define RTLD_NEXT ((void *) -1l)
#define RTLD_DEFAULT ((void *) 0)

extern void dlogf(enum shadow_log_code level, char *fmt, ...);

#ifdef DEBUG
#define PRELOADLOGD(fmt, ...) dlogf(LOG_DEBUG, fmt, ## __VA_ARGS__)
#else
#define PRELOADLOGD(fmt, ...)
#endif

#define PRELOADLOG(level, fmt, ...) dlogf(level, fmt, ## __VA_ARGS__)

/* convenience macro for doing the dlsym lookups
 * return x if function can't be found */
#define PRELOAD_LOOKUP(my_function, my_search, ret) \
{ \
	/* only search if function pointer is null */ \
	if(*my_function == NULL){ \
		/* if we dont have logging, then we are not running shadow yet, gtfo */ \
		/* clear old error vals */ \
		dlerror(); \
		/* search for a function symbol that tells us shadow is loaded */ \
		void* log_fp = dlsym(RTLD_NEXT, "intercept_time"); \
		/* check for error, dlerror returns null or a char* error msg */ \
		char* dlmsg = dlerror(); \
		if(!log_fp || dlmsg != NULL) { \
			return ret; \
		} \
		/* we have log function, clear old error vals */ \
		dlerror(); \
		/* search for function symbol */ \
		*my_function = dlsym(RTLD_NEXT, my_search); \
		/* check for error, dlerror returns null or a char* error msg */ \
		dlmsg = dlerror(); \
		if (!*my_function || dlmsg != NULL) { \
			PRELOADLOG(LOG_CRIT, "PRELOAD_LOOKUP: failed to chain-load function \"%s\": dlerror = \"%s\", fp = \"%p\"\n", my_search, dlmsg, *my_function); \
			return ret; \
		} \
	} \
	PRELOADLOGD("PRELOAD_LOOKUP: calling \"%s\"\n", my_search); \
}

#endif /* PRELOAD_H_ */
