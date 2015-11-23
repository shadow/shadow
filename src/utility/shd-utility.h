/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_UTILITY_H_
#define SHD_UTILITY_H_

#include "shadow.h"

#ifdef DEBUG
#define utility_assert(expr) \
do { \
    if G_LIKELY (expr) { \
        ; \
    } else { \
        utility_handleError(__FILE__, __LINE__, G_STRFUNC, #expr); \
    } \
} while (0)
#else
#define utility_assert(expr)
#endif

guint utility_ipPortHash(in_addr_t ip, in_port_t port);
guint utility_int16Hash(gconstpointer value);
gboolean utility_int16Equal(gconstpointer value1, gconstpointer value2);
gint utility_doubleCompare(const gdouble* value1, const gdouble* value2, gpointer userData);
gint utility_simulationTimeCompare(const SimulationTime* value1, const SimulationTime* value2,
        gpointer userData);
gchar* utility_getHomePath(const gchar* path);
guint utility_getRawCPUFrequency(const gchar* freqFilename);
gboolean utility_isRandomPath(const gchar* path);

gboolean utility_removeAll(const gchar* path);
gboolean utility_copyAll(const gchar* srcPath, const gchar* dstPath);

void utility_handleError(const gchar* file, gint line, const gchar* funtcion, const gchar* message);

#endif /* SHD_UTILITY_H_ */
