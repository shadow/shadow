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

#ifndef SHD_PLUGIN_H_
#define SHD_PLUGIN_H_

#include <glib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "log_codes.h"
#include "snricall_codes.h"

/**
 * SNRI R2 - Standard Network Routing Interface Revision 2
 * Loadable module header
 */

/* snricall function poginter */
typedef gint (*_snricall_fpref_t)(gint, ...);
extern _snricall_fpref_t _snricall_fpref;
#define _snricall (*_snricall_fpref)

/* This is the function poginter that will need to be passed for timer callback. When your
 * timer expires, SNRI will call the function you pass with this signature.
 */
typedef void (*snri_timer_callback_fp)(
			gint,  			/* timer id */
			gpointer 			/* saved argument */
		);

/* signature for the functions used to create a timer */
typedef gint (*snri_create_timer_fp)(gint, snri_timer_callback_fp, gpointer );

/**
 * @param t Will be filled with the current system time
 */
gint snri_gettime(struct timeval *t);

/**
 * Creates a timer that will expire after the given delay
 *
 * @param milli_delay Number of milliseconds after which this timer will expire
 * @param callback_function The function that will be called when this timer expires
 */
gint snri_timer_create(gint milli_delay, snri_timer_callback_fp callback_function, gpointer cb_arg);

/**
 * Destroys the timer with the given ID, preventing it from executing
 */
gint snri_timer_destroy(gint timer_id);

/**
 * Schedules this node for deletion
 */
gint snri_exit(void);

/**
 * Logs some message at the given log_level. Will only be prginted if logged
 * at a lower level than the configured system log level.
 *
 * this function takes the args:
 * gint log_level, gchar* format, ...
 */
#define snri_log(log_level, format, ...) _snricall(SNRICALL_LOG, log_level, format, ## __VA_ARGS__)
#define snri_logdebug(format, ...) _snricall(SNRICALL_LOG, LOG_DEBUG, format, ## __VA_ARGS__)

/**
 * Logs some binary data. Level indicates the logging level; you should use 0 for critical messages, and increasingly
 * high numbers for higher and higher verbosities.
 */
gint snri_log_binary(gint level, gchar * data, gint data_size);

/**
 * resolves name to an address and stores the result in addr_out. name MUST be a
 * NULL terminated string or the behavior is undefined.
 */
gint snri_resolve_name(gchar* name, in_addr_t* addr_out);

/**
 * resolves address to a name and stores the result in name_out. name_out should
 * pogint to a buffer of length name_out_len to avoid overflows. if the buffer name_out
 * pogints to is smaller than the actual hostname, this will return an error.
 */
gint snri_resolve_addr(in_addr_t addr, gchar* name_out, gint name_out_len);

/**
 * resolves the node given by addr and returns the minimum of its configured
 * upload and download bandwidth in killobytes per second ginto bw_KBps_out.
 * if addr is not mapped, bw_KBps_out will be set to 0. always returns SNRI_SUCCESS.
 */
gint snri_resolve_minbw(in_addr_t addr, guint* bw_KBps_out);

/**
 * get local node's ip address
 *
 * @return IP of this node
 */
gint snri_getip(in_addr_t* addr_out);

/**
 * returns the hostname of the caller. if the buffer name_out pogints to is
 * smaller than the actual hostname, this will return an error.
 */
gint snri_gethostname(gchar* name_out, gint name_out_len);

/*
 * returns 1 if the virtual socket referred to by sockd exists and is
 * ready for reading, -1 on error, 0 otherwise.
 */
gint snri_socket_is_readable(gint sockd);

/*
 * returns 1 if the virtual socket referred to by sockd exists and is
 * ready for writing, -1 on error, 0 otherwise.
 */
gint snri_socket_is_writable(gint sockd);

/*
 * set the callback that will called whenever libevent loopexit function gets called
 */
gint snri_set_loopexit_fn(snri_timer_callback_fp fn);

/**
 * Registers the set of globals for this module.
 * This should be done when the module is created and gets the _snri_mod_init function called
 *
 * @param num_globals Number of globals to register
 * The rest of the params should follow this form:
 * 		global variable poginter
 * 		size of data
 *
 * For instance, you may have the following globals:
 *   gint a; gchar b;
 *
 * So, you'd call:
 *
 *  snri_regsiter_globals(2, &a, sizeof(a), &b, sizeof(b));
 *
 * Simple!
 */
#define snri_register_globals(...) _snricall(SNRICALL_REGISTER_GLOBALS, __VA_ARGS__)

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 01000000
#endif

#endif /* SHD_PLUGIN_H_ */
