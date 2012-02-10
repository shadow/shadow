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

#ifndef PRELOAD_H_
#define PRELOAD_H_

#include <glib.h>
#include <dlfcn.h>
#include <stdio.h>

#define INTERCEPT_PREFIX "intercept_"

/* handles for dlsym */
#define RTLD_NEXT ((void *) -1l)
#define RTLD_DEFAULT ((void *) 0)

/**
 * @warning
 * Its not ok to call shadow function from the preload lib. it is not linked
 * to shadow, so it has to search for the symbols. further, it can only call
 * functions in the intercept lib, because the search only works if the symbol
 * exists in other shared libraries.
 */

/**
 * Searches for the worker_isInShadowContext function and calls it
 * @return 1 if we are in shadow context, 0 if we are in plug-in context
 *
 * @see worker_isInShadowContext()
 */
int preload_worker_isInShadowContext();

// todo logging causes too much recursion, because something in the log function gets intercepted
//extern void logging_log(const gchar *log_domain, GLogLevelFlags log_level, const gchar* functionName, const gchar *format, ...);

/** convenience macro for doing the dlsym lookups
 * return "ret" if function can't be found. if used in a void function, use a comma
 * but omit the last param, like "PRELOAD_LOOKUP(a, b,)" */
#define PRELOAD_LOOKUP(my_function, my_search, ret) \
{ \
	/* only search if function pointer is null */ \
	if(*my_function == NULL){ \
		/* if we dont have logging, then we are not running shadow yet, gtfo */ \
		/* clear old error vals */ \
		dlerror(); \
		/* search for a function symbol that tells us shadow is loaded */ \
		void* functionInShadowInterceptLib = dlsym(RTLD_NEXT, "intercept_time"); \
		/* check for error, dlerror returns null or a char* error msg */ \
		char* dlmsg = dlerror(); \
		if(!functionInShadowInterceptLib || dlmsg != NULL) { \
			/* g_error("PRELOAD_LOOKUP: Shadow is not loaded: no function \"%s\": dlerror = \"%s\", fp = \"%p\"\n", my_search, dlmsg, *my_function); */ \
			return ret; \
		} \
		/* we have a shadow function, clear old error vals */ \
		dlerror(); \
		/* search for function symbol */ \
		*my_function = dlsym(RTLD_NEXT, my_search); \
		/* check for error, dlerror returns null or a char* error msg */ \
		dlmsg = dlerror(); \
		if (!*my_function || dlmsg != NULL) { \
			/* g_error("PRELOAD_LOOKUP: failed to chain-load function \"%s\": dlerror = \"%s\", fp = \"%p\"\n", my_search, dlmsg, *my_function); */ \
			return ret; \
		} \
	} \
	/* debug("PRELOAD_LOOKUP: calling \"%s\"\n", my_search); */ \
}

/** convenience macro for deciding if we should intercept the function or
 * not, depending on if the call came from shadow or the plug-in.
 * if the plugin exists, we came from the plugin, and the extra condition holds,
 * we attempt to redirect the call to shadow.
 * pass in "1" for extraCondition if you ALWAYS redirect when coming from a
 * plug-in (i.e. you have no special condition like socket >= MIN_DESCRIPTOR). */
#define PRELOAD_DECIDE(funcOut, nameOut, sysName, sysPointer, shadowPrefix, shadowPointer, extraCondition) \
{ \
	/* should we be forwarding to the system call? */ \
	if(!preload_worker_isInShadowContext() && extraCondition) { \
		funcOut = &shadowPointer; \
		nameOut = shadowPrefix sysName; \
	} else { \
		funcOut = &sysPointer; \
		nameOut = sysName; \
	} \
}

#endif /* PRELOAD_H_ */
