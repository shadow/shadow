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

#ifndef SHD_PLUGIN_FILEGETTER_UTIL_H_
#define SHD_PLUGIN_FILEGETTER_UTIL_H_

#include <glib.h>
#include <netinet/in.h>

#include "shd-service-filegetter.h"

void plugin_filegetter_util_log_callback(enum service_filegetter_loglevel level, const gchar* message);
in_addr_t plugin_filegetter_util_hostbyname_callback(const gchar* hostname);
void plugin_filegetter_util_wakeup_callback(gint timerid, gpointer arg);
void plugin_filegetter_util_sleep_callback(gpointer sfg, guint seconds);

#endif /* SHD_PLUGIN_FILEGETTER_UTIL_H_ */
