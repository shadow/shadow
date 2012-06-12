#ifndef SHD_BROWSER_H_
#define SHD_BROWSER_H_

#include <glib.h>
#include <glib/gprintf.h>
#include <libxml/HTMLparser.h>

void ignore_xml_errors(void* ctx, const char* msg, ...);
gchar* get_url_base(gchar*);
gchar** crack_url(gchar*, gint*);
void find_objects(htmlNodePtr, GSList**);
void parse_html(gchar*, GSList**);
gchar* parse_img(GHashTable*);
gchar* parse_link(GHashTable*);
gchar* parse_script(GHashTable*);
GHashTable* get_attributes(htmlNodePtr);
gboolean url_is_absolute(const gchar*);
gchar* get_hostname_from_url(gchar*);

#endif /* SHD_BROWSER_H_ */