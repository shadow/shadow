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

#include "shd-html.h"

static gchar* html_parse_img(GHashTable* attrs) {
	return g_hash_table_lookup(attrs, "src");
}

static gchar* html_parse_link(GHashTable* attrs) {
	gchar* rel = g_hash_table_lookup(attrs, "rel");
	gchar* source = g_hash_table_lookup(attrs, "href");

	if (g_strcmp0(rel, "stylesheet") == 0) {
		return source;
	}

	if (g_strcmp0(rel, "shortcut icon") == 0) {
		return source;
	}

	return NULL;
}

static gchar* html_parse_script(GHashTable* attrs) {
	gchar* rel = g_hash_table_lookup(attrs, "type");
	gchar* source = g_hash_table_lookup(attrs, "src");

	if (g_strcmp0(rel, "text/javascript") == 0 && source != NULL) {
		return source;
	}

	return NULL;
}

static GHashTable* html_get_attributes(TidyNode node) {
	TidyAttr curr_attr = tidyAttrFirst(node);
	GHashTable* attrs = g_hash_table_new(g_str_hash, g_str_equal);
	gchar* canonical_name = NULL, * value = NULL;

	while(curr_attr) {
		canonical_name = g_utf8_strdown(tidyAttrName(curr_attr), -1);
		value = g_strdup(tidyAttrValue(curr_attr));
		g_hash_table_insert(attrs, canonical_name, value);
		curr_attr = tidyAttrNext(curr_attr);
	}

	return attrs;
}

static void html_find_objects(TidyNode node, GSList** objs) {
	TidyNode child;
	gchar* url = NULL;
	const gchar* name = NULL;
	GHashTable* attrs = NULL;
  
	for (child = tidyGetChild(node); child; child = tidyGetNext(child)) {
		attrs = html_get_attributes(child);
		
		if ((name = tidyNodeGetName(child))) {
			if (g_ascii_strncasecmp(name, "img", 3) == 0) {
				url = html_parse_img(attrs);
			} else if (g_ascii_strncasecmp(name, "script", 6) == 0) {
				url = html_parse_script(attrs);
			} else if (g_ascii_strncasecmp(name, "link", 4) == 0) {
				url = html_parse_link(attrs);
			}

			g_hash_table_destroy(attrs);

			if (url != NULL) {
				*objs = g_slist_append(*objs, g_strdup(url));  
				url = NULL;
			} 
		}

		html_find_objects(child, objs);
	}
}

void html_parse(const gchar* html, GSList** objs) {
	TidyDoc tdoc = tidyCreate();
	TidyBuffer tidy_errbuf = {0};
	int err = 0;
  
	tidyOptSetBool(tdoc, TidyForceOutput, yes); /* try harder */ 
	tidyOptSetInt(tdoc, TidyWrapLen, 4096);
	tidySetErrorBuffer( tdoc, &tidy_errbuf );
    
	err = tidyParseString(tdoc, html); /* parse the input */ 
	
	if ( err >= 0 ) {
		err = tidyCleanAndRepair(tdoc); /* fix any problems */ 
		
		if ( err >= 0 ) {
			err = tidyRunDiagnostics(tdoc); /* load tidy error buffer */ 
			
			if ( err >= 0 ) {
				html_find_objects(tidyGetHtml(tdoc), objs); /* walk the tree */ 
			}
		}
	}
}
