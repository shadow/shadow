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

#include "shd-url.h"

static gchar** url_crack(const gchar* url) {
	gchar *url_pattern = "^(http[s]?:\\/\\/|\\/\\/)([^\\/]+)((.*?)([^\\/]*))$";
	gchar** parts;
	gint match_count;
	GRegex *regex = g_regex_new(url_pattern, G_REGEX_CASELESS, 0, NULL);
	GMatchInfo *match_info;
	
	g_regex_match(regex, url, 0, &match_info);
	parts = g_match_info_fetch_all(match_info);
	match_count = g_match_info_get_match_count(match_info);
	
	/* clean up */
	g_match_info_free(match_info);
	g_regex_unref(regex);
	
	/* URL is malforned*/
	if (match_count < 6) {
		g_strfreev(parts);
		return NULL;
	}

	return parts;
}

gint url_get_parts(const gchar* url, gchar** hostname, gchar** path) {
	gchar** parts = url_crack(url);

	if (parts == NULL) {    
		return -1;
	}

	*hostname = g_utf8_strdown(g_strdup(parts[2]), -1);
	*path = g_strdup(parts[3]);
	g_strfreev(parts);

	return 0;
}

gboolean url_is_absolute(const gchar* url) {    
	return g_str_has_prefix(url, "http://") || 
		g_str_has_prefix(url, "https://") ||
		g_str_has_prefix(url, "//");
}
