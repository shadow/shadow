/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
