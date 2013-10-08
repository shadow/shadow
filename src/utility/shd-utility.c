/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include <execinfo.h>

guint utility_ipPortHash(in_addr_t ip, in_port_t port) {
	GString* buffer = g_string_new(NULL);
	g_string_printf(buffer, "%u:%u", ip, port);
	guint hash_value = g_str_hash(buffer->str);
	g_string_free(buffer, TRUE);
	return hash_value;
}

guint utility_int16Hash(gconstpointer value) {
	utility_assert(value);
	/* make sure upper bits are zero */
	gint key = 0;
	key = (gint) *((gint16*)value);
	return g_int_hash(&key);
}

gboolean utility_int16Equal(gconstpointer value1, gconstpointer value2) {
	utility_assert(value1 && value2);
	/* make sure upper bits are zero */
	gint key1 = 0, key2 = 0;
	key1 = (gint) *((gint16*)value1);
	key2 = (gint) *((gint16*)value2);
	return g_int_equal(&key1, &key2);
}

gint utility_doubleCompare(const gdouble* value1, const gdouble* value2, gpointer userData) {
	utility_assert(value1 && value2);
	/* return neg if first before second, pos if second before first, 0 if equal */
	return value1 == value2 ? 0 : value1 < value2 ? -1 : +1;
}

gchar* utility_getHomePath(const gchar* path) {
	GString* sbuffer = g_string_new("");
	if(g_ascii_strncasecmp(path, "~", 1) == 0) {
		/* replace ~ with home directory */
		const gchar* home = g_get_home_dir();
		g_string_append_printf(sbuffer, "%s%s", home, path+1);
	} else {
		g_string_append_printf(sbuffer, "%s", path);
	}
	return g_string_free(sbuffer, FALSE);
}

guint utility_getRawCPUFrequency(const gchar* freqFilename) {
	/* get the raw speed of the experiment machine */
	guint rawFrequencyKHz = 0;
	gchar* contents = NULL;
	gsize length = 0;
	GError* error = NULL;
	if(freqFilename && g_file_get_contents(freqFilename, &contents, &length, &error)) {
		utility_assert(contents);
		rawFrequencyKHz = (guint)atoi(contents);
		g_free(contents);
	}
	if(error) {
		g_error_free(error);
	}
	return rawFrequencyKHz;
}

void utility_printBacktrace() {
	g_print("%s", "**BEGIN BACKTRACE**\n");
	void *array[50];
	gsize size, i;
	gchar **strings;

	size = backtrace(array, 50);
	strings = backtrace_symbols(array, size);

	g_print("Obtained %zd stack frames:\n", size);

	for (i = 0; i < size; i++) {
		g_print("\t%s\n", strings[i]);
	}

	g_free(strings);
	g_print("%s", "**END BACKTRACE**\n");
}
