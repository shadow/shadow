/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <glib/gstdio.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/utility/utility.h"

guint utility_ipPortHash(in_addr_t ip, in_port_t port) {
    GString* buffer = g_string_new(NULL);
    g_string_printf(buffer, "%u:%u", ip, port);
    guint hash_value = g_str_hash(buffer->str);
    g_string_free(buffer, TRUE);
    return hash_value;
}

gint utility_simulationTimeCompare(const CSimulationTime* value1, const CSimulationTime* value2,
                                   gpointer userData) {
    utility_debugAssert(value1 && value2);
    /* return neg if first before second, pos if second before first, 0 if equal */
    return (*value1) == (*value2) ? 0 : (*value1) < (*value2) ? -1 : +1;
}

static GString* _utility_formatError(const gchar* file, gint line, const gchar* function,
                                     const gchar* message, va_list vargs) {
    GString* errorString = g_string_new("**ERROR ENCOUNTERED**\n");
    g_string_append_printf(errorString, "\tAt process: %i (parent %i)\n", (gint) getpid(), (gint) getppid());
    g_string_append_printf(errorString, "\tAt file: %s\n", file);
    g_string_append_printf(errorString, "\tAt line: %i\n", line);
    g_string_append_printf(errorString, "\tAt function: %s\n", function);
    g_string_append_printf(errorString, "\tMessage: ");
    g_string_append_vprintf(errorString, message, vargs);
    g_string_append_printf(errorString, "\n");
    return errorString;
}

void utility_handleError(const gchar* file, gint line, const gchar* function, const gchar* message,
                         ...) {
    logger_flush(logger_getDefault());

    va_list vargs;
    va_start(vargs, message);
    GString* errorString = _utility_formatError(file, line, function, message, vargs);
    va_end(vargs);

    char* backtraceString = backtrace();

    if (!isatty(fileno(stdout))) {
        g_print("%s**BEGIN BACKTRACE**\n%s\n**END BACKTRACE**\n**ABORTING**\n", errorString->str,
                backtraceString);
    }
    g_printerr("%s**BEGIN BACKTRACE**\n%s\n**END BACKTRACE**\n**ABORTING**\n", errorString->str,
               backtraceString);

    g_string_free(errorString, TRUE);
    backtrace_free(backtraceString);
    abort();
}

gboolean utility_isRandomPath(const gchar* path) {
    if(path) {
        return !g_ascii_strcasecmp(path, "/dev/random") ||
           !g_ascii_strcasecmp(path, "/dev/urandom") ||
           !g_ascii_strcasecmp(path, "/dev/srandom");
    } else {
        return FALSE;
    }
}

gchar* utility_strvToNewStr(gchar** strv) {
    GString* strBuffer = g_string_new(NULL);

    if(strv) {
        for(gint i = 0; strv[i] != NULL; i++) {
            if(strv[i+1] == NULL) {
                g_string_append_printf(strBuffer, "%s", strv[i]);
            } else {
                g_string_append_printf(strBuffer, "%s ", strv[i]);
            }
        }
    }

    return g_string_free(strBuffer, FALSE);
}
