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
