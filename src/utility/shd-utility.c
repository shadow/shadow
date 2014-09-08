/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include <execinfo.h>
#include <sys/types.h>
#include <unistd.h>

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
	return *value1 == *value2 ? 0 : *value1 < *value2 ? -1 : +1;
}

gint utility_simulationTimeCompare(const SimulationTime* value1, const SimulationTime* value2,
		gpointer userData) {
	utility_assert(value1 && value2);
	/* return neg if first before second, pos if second before first, 0 if equal */
	return (*value1) == (*value2) ? 0 : (*value1) < (*value2) ? -1 : +1;
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

static GString* _utility_formatError(const gchar* file, gint line, const gchar* function, const gchar* message) {
	GString* errorString = g_string_new("**ERROR ENCOUNTERED**\n");
	g_string_append_printf(errorString, "\tAt process: %i (parent %i)\n", (gint) getpid(), (gint) getppid());
	g_string_append_printf(errorString, "\tAt file: %s\n", file);
	g_string_append_printf(errorString, "\tAt line: %i\n", line);
	g_string_append_printf(errorString, "\tAt function: %s\n", function);
	g_string_append_printf(errorString, "\tMessage: %s\n", message);
	return errorString;
}

static GString* _utility_formatBacktrace() {
	GString* backtraceString = g_string_new("**BEGIN BACKTRACE**\n");
	void *array[100];
	gsize size, i;
	gchar **strings;

	size = backtrace(array, 100);
	strings = backtrace_symbols(array, size);

	g_string_append_printf(backtraceString, "Obtained %zd stack frames:\n", size);

	for (i = 0; i < size; i++) {
		g_string_append_printf(backtraceString, "\t%s\n", strings[i]);
	}

	g_free(strings);
	g_string_append_printf(backtraceString, "**END BACKTRACE**\n");
	return backtraceString;
}

void utility_handleError(const gchar* file, gint line, const gchar* function, const gchar* message) {
	GString* errorString = _utility_formatError(file, line, function, message);
	GString* backtraceString = _utility_formatBacktrace();
	if(!isatty(fileno(stdout))) {
		g_print("%s%s**ABORTING**\n", errorString->str, backtraceString->str);
	}
	g_printerr("%s%s**ABORTING**\n", errorString->str, backtraceString->str);
	g_string_free(errorString, TRUE);
	g_string_free(backtraceString, TRUE);
	abort();
}
