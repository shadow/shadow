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

#include <time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "shd-plugin-filegetter-util.h"
#include "shd-service-filegetter.h"
#include "shd-plugin.h"

void plugin_filegetter_util_log_callback(enum service_filegetter_loglevel level, const char* message) {
	if(level == SFG_CRITICAL) {
		snri_log(LOG_CRIT, "%s\n", message);
	} else if(level == SFG_WARNING) {
		snri_log(LOG_WARN, "%s\n", message);
	} else if(level == SFG_NOTICE) {
		snri_log(LOG_MSG, "%s\n", message);
	} else if(level == SFG_INFO) {
		snri_log(LOG_INFO, "%s\n", message);
	} else if(level == SFG_DEBUG) {
		snri_logdebug("%s\n", message);
	} else {
		/* we dont care */
	}
}

in_addr_t plugin_filegetter_util_hostbyname_callback(const char* hostname) {
	in_addr_t addr = 0;

	/* get the address in network order */
	if(strncmp(hostname, "none", 4) == 0) {
		addr = htonl(INADDR_NONE);
	} else if(strncmp(hostname, "localhost", 9) == 0) {
		addr = htonl(INADDR_LOOPBACK);
	} else {
		if(snri_resolve_name((char*)hostname, &addr) != SNRICALL_SUCCESS) {
			snri_log(LOG_WARN, "%s does not resolve to a usable address\n", hostname);
			addr = htonl(INADDR_NONE);
		}
	}

	return addr;
}

void plugin_filegetter_util_wakeup_callback(int timerid, void* arg) {
	service_filegetter_activate((service_filegetter_tp) arg, 0);
}

void plugin_filegetter_util_sleep_callback(void* sfg, unsigned int seconds) {
	if(snri_timer_create(seconds * 1000, &plugin_filegetter_util_wakeup_callback, sfg) == SNRICALL_ERROR) {
		snri_log(LOG_WARN, "unable to create sleep timer for %u seconds\n", seconds);
	}
}
