/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_UTILITY_H_
#define SHD_UTILITY_H_

#include "shadow.h"

#define utility_assert(expr) \
do { \
	if G_LIKELY (expr) ; \
	else { \
		utility_printBacktrace(); \
		g_assertion_message_expr (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,  #expr); \
	} \
} while (0)

guint utility_ipPortHash(in_addr_t ip, in_port_t port);
guint utility_int16Hash(gconstpointer value);
gboolean utility_int16Equal(gconstpointer value1, gconstpointer value2);
gint utility_doubleCompare(const gdouble* value1, const gdouble* value2, gpointer userData);
gchar* utility_getHomePath(const gchar* path);
guint utility_getRawCPUFrequency(const gchar* freqFilename);
void utility_printBacktrace();

#endif /* SHD_UTILITY_H_ */
