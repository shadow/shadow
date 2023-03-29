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

void utility_handleError(const gchar* file, gint line, const gchar* function, const gchar* message,
                         ...) {
    va_list vargs;
    va_start(vargs, message);
    utility_handleErrorInner(file, line, function, message, vargs);
    va_end(vargs);
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
