/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
