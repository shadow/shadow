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

#include "shd-browser.h"

static void ignore_xml_errors(void* ctx, const char* msg, ...) { }

static gchar* parse_img(GHashTable* attrs) {
	return g_hash_table_lookup(attrs, "src");
}

static gchar* parse_link(GHashTable* attrs) {
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

static gchar* parse_script(GHashTable* attrs) {
	gchar* rel = g_hash_table_lookup(attrs, "type");
	gchar* source = g_hash_table_lookup(attrs, "src");

	if (g_strcmp0(rel, "text/javascript") == 0 && source != NULL) {
		return source;
	}

	return NULL;
}

static GHashTable* get_attributes(htmlNodePtr node) {
	xmlAttrPtr curr_attr;
	gchar* canonical_name;
	GHashTable* attrs = g_hash_table_new(g_str_hash, g_str_equal);

	for(curr_attr = node->properties; curr_attr != NULL; curr_attr = curr_attr->next) {
		canonical_name = g_utf8_strdown((const gchar*) curr_attr->name, -1);
		g_hash_table_insert(attrs, canonical_name, curr_attr->children->content);
	}

	return attrs;
}

static void find_objects(htmlNodePtr element, GSList** objs) {
	htmlNodePtr node = element;
	gchar* url;
	GHashTable* attrs;
  
	for(; node != NULL; node = node->next) {
		if(node->type == XML_ELEMENT_NODE) {
			attrs = get_attributes(node);
			
			if (g_ascii_strncasecmp((const gchar*) node->name, "img", 3) == 0) {
				url = parse_img(attrs);
			} else if (g_ascii_strncasecmp((const gchar*) node->name, "script", 6) == 0) {
				url = parse_script(attrs);
			} else if (g_ascii_strncasecmp((const gchar*) node->name, "link", 4) == 0) {
				url = parse_link(attrs);
			}
			
			g_hash_table_destroy(attrs);

			if (url != NULL) {
				*objs = g_slist_append(*objs, g_strdup(url));  
				url = NULL;
			}
		}
      
		if(node->children != NULL) {
			find_objects(node->children, objs);
		}
	}
}

static void parse_html(const gchar* html, GSList** objs) {
	xmlSetGenericErrorFunc(NULL, ignore_xml_errors);
	htmlDocPtr doc = htmlParseDoc((xmlChar*) html, "UTF-8");

	if(doc != NULL) {
		htmlNodePtr root = xmlDocGetRootElement(doc);

		if(root != NULL) {
			find_objects(root, objs);
		}

		xmlFreeDoc(doc);
		doc = NULL;
	}
}

static gchar** crack_url(const gchar* url) {
	gchar *url_pattern = "^(http[s]?:\\/\\/)([^\\/]+)((.*?)([^\\/]*))$";
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

static gint get_url_parts(const gchar* url, gchar** hostname, gchar** path) {
	gchar** parts = crack_url(url);

	if (parts == NULL) {    
		return -1;
	}

	*hostname = g_strdup(parts[2]);
	*path = g_strdup(parts[3]);
	g_strfreev(parts);

	return 0;
}

static gboolean url_is_absolute(const gchar* url) {    
	if (url) {
		const gchar* ptr = url;

		while (*ptr) {
			if (*ptr == ':') return TRUE;
			if (*ptr == '/' || *ptr == '?' || *ptr == '#') break;
			ptr++;
		}
	}

	return FALSE;
}

GHashTable* get_embedded_objects(service_filegetter_tp sfg, gint* obj_count) {
	GSList* objs = NULL;
	gchar* html = g_string_free(sfg->fg.content, FALSE);
	GHashTable* download_tasks = g_hash_table_new(g_str_hash, g_str_equal);
	
	/* Parse with libxml2. The result is a linked list with all relative and absolute URLs */
	parse_html(html, &objs);
	
	while (objs != NULL) {
		gchar* url = (gchar*) objs->data;
		gchar* hostname = NULL;
		gchar* path = NULL;
		
		if (url_is_absolute(url)) {
			get_url_parts(url, &hostname ,&path);
		} else {
			hostname = sfg->browser->first_hostname;
			path = g_strdup(url);
		}
		
		browser_download_tasks_tp tasks = g_hash_table_lookup(download_tasks, hostname);

		/* If for the first time a hostname is used initialize a new queue */
		if (tasks == NULL) {
			tasks = g_malloc(sizeof(browser_download_tasks_t));
			tasks->unfinished = g_queue_new();
			tasks->running = NULL;
			g_hash_table_insert(download_tasks, hostname, tasks);
		}

		g_printf("download_tasks: %s -> %s\n", hostname, path);

		/* Add the actual URL to the end of the queue */
		g_queue_push_tail(tasks->unfinished, url);
		
		(*obj_count)++;
		objs = g_slist_next(objs);
	}
	
	g_slist_free_full(objs, NULL);
	return download_tasks;
}
