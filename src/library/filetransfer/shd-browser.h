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

#ifndef SHD_BROWSER_H_
#define SHD_BROWSER_H_

#include <glib.h>
#include <glib/gprintf.h>
#include <netinet/in.h>
#include <libxml/HTMLparser.h>

#include "shd-service-filegetter.h"

typedef struct service_filegetter_s service_filegetter_t, *service_filegetter_tp;

typedef struct browser_download_tasks_s {
  GQueue* unfinished;
  GSList* running;
} browser_download_tasks_t, *browser_download_tasks_tp;

GHashTable* get_embedded_objects(service_filegetter_tp);

#endif /* SHD_BROWSER_H_ */
