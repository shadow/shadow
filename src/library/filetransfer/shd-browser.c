#include <glib.h>
#include <glib/gprintf.h>
#include <libxml/HTMLparser.h>

#include "shd-browser.h"

void ignore_xml_errors(void* ctx, const char* msg, ...) {
  return;
}

gchar* get_url_base(gchar* url) {
  gint match_count;
  GString* base = g_string_new("");
  gchar** parts = crack_url(url, &match_count);
  
  if (match_count < 3) {    
    g_fprintf(stderr, "Malformed url: %s\n", url);
    exit(EXIT_FAILURE); 
  }

  g_string_append(base, parts[1]);
  g_string_append(base, parts[2]);
  g_strfreev(parts);
  
  return g_string_free(base, FALSE);
}

gchar** crack_url(gchar* url, gint* match_count) {
  gchar *url_pattern;
  gchar** parts;
  GRegex *regex;
  GMatchInfo *match_info;
  
  if (!url_is_absolute(url)) {
    g_fprintf(stderr, "URL is not absolute: <%s>\n", url);
    exit(EXIT_FAILURE); 
  }
  
  url_pattern = "^(http[s]?:\\/\\/)([^\\/]+)(.*?)([^\\/]*)$";
  regex = g_regex_new(url_pattern, G_REGEX_CASELESS, 0, NULL);
  g_regex_match(regex, url, 0, &match_info);
  parts = g_match_info_fetch_all(match_info);
  *match_count = g_match_info_get_match_count(match_info);
  g_match_info_free(match_info);
  g_regex_unref(regex);
  
  return parts;
}

void find_objects(htmlNodePtr element, GSList** objs) {
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

void parse_html(gchar* html, GSList** objs) {
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

gchar* parse_img(GHashTable* attrs) {
	return g_hash_table_lookup(attrs, "src");
}

gchar* parse_link(GHashTable* attrs) {
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

gchar* parse_script(GHashTable* attrs) {
  gchar* rel = g_hash_table_lookup(attrs, "type");
  gchar* source = g_hash_table_lookup(attrs, "src");
  
  if (g_strcmp0(rel, "text/javascript") == 0 && source != NULL) {
    return source;
  }
  
  return NULL;
}

GHashTable* get_attributes(htmlNodePtr node) {
  xmlAttrPtr curr_attr;
  gchar* canonical_name;
  GHashTable* attrs = g_hash_table_new(g_str_hash, g_str_equal);
  
  for(curr_attr = node->properties; curr_attr != NULL; curr_attr = curr_attr->next) {
    canonical_name = g_utf8_strdown((const gchar*) curr_attr->name, -1);
    g_hash_table_insert(attrs, canonical_name, curr_attr->children->content);
  }
  
  return attrs;
}

gboolean url_is_absolute(const gchar* url) {    
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

gchar* get_hostname_from_url(gchar* url) {
  gint match_count;
  gchar* hostname;
  gchar** parts = crack_url(url, &match_count);
  
  if (match_count < 3) {    
    g_fprintf(stderr, "Malformed url: %s\n", url);
    exit(EXIT_FAILURE); 
  }

  hostname = g_strdup(parts[2]);
  g_strfreev(parts);
  
  return hostname;
}
