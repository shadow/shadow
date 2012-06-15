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

static void html_ignore_errors(void* ctx, const char* msg, ...) { }

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

static GHashTable* html_get_attributes(htmlNodePtr node) {
	xmlAttrPtr curr_attr;
	gchar* canonical_name;
	GHashTable* attrs = g_hash_table_new(g_str_hash, g_str_equal);

	for(curr_attr = node->properties; curr_attr != NULL; curr_attr = curr_attr->next) {
		canonical_name = g_utf8_strdown((const gchar*) curr_attr->name, -1);
		g_hash_table_insert(attrs, canonical_name, curr_attr->children->content);
	}

	return attrs;
}

static void html_find_objects(htmlNodePtr element, GSList** objs) {
	htmlNodePtr node = element;
	gchar* url;
	GHashTable* attrs;
  
	for(; node != NULL; node = node->next) {
		if(node->type == XML_ELEMENT_NODE) {
			attrs = html_get_attributes(node);
			
			if (g_ascii_strncasecmp((const gchar*) node->name, "img", 3) == 0) {
				url = html_parse_img(attrs);
			} else if (g_ascii_strncasecmp((const gchar*) node->name, "script", 6) == 0) {
				url = html_parse_script(attrs);
			} else if (g_ascii_strncasecmp((const gchar*) node->name, "link", 4) == 0) {
				url = html_parse_link(attrs);
			}
			
			g_hash_table_destroy(attrs);

			if (url != NULL) {
				*objs = g_slist_append(*objs, g_strdup(url));  
				url = NULL;
			}
		}
      
		if(node->children != NULL) {
			html_find_objects(node->children, objs);
		}
	}
}

void html_parse(const gchar* html, GSList** objs) {
	xmlSetGenericErrorFunc(NULL, html_ignore_errors);
	htmlDocPtr doc = htmlParseDoc((xmlChar*) html, "UTF-8");

	if(doc != NULL) {
		htmlNodePtr root = xmlDocGetRootElement(doc);

		if(root != NULL) {
			html_find_objects(root, objs);
		}

		xmlFreeDoc(doc);
		doc = NULL;
	}
}
