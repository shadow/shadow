/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_URL_H_
#define SHD_URL_H_

#include <glib.h>

gint url_get_parts(const gchar* url, gchar** hostname, gchar** path);
gboolean url_is_absolute(const gchar* url);

#endif /* SHD_URL_H_ */
