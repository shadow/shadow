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
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/utility/utility.h"

void utility_handleError(const char* file, int line, const char* function, const char* message,
                         ...) {
    va_list vargs;
    va_start(vargs, message);
    utility_handleErrorInner(file, line, function, message, vargs);
    va_end(vargs);
}

bool utility_isRandomPath(const char* path) {
    if(path) {
        return !g_ascii_strcasecmp(path, "/dev/random") ||
           !g_ascii_strcasecmp(path, "/dev/urandom") ||
           !g_ascii_strcasecmp(path, "/dev/srandom");
    } else {
        return FALSE;
    }
}

char* utility_strvToNewStr(char** strv) {
    GString* strBuffer = g_string_new(NULL);

    if(strv) {
        for (int i = 0; strv[i] != NULL; i++) {
            if(strv[i+1] == NULL) {
                g_string_append_printf(strBuffer, "%s", strv[i]);
            } else {
                g_string_append_printf(strBuffer, "%s ", strv[i]);
            }
        }
    }

    return g_string_free(strBuffer, FALSE);
}

// Address must be in network byte order.
char* util_ipToNewString(in_addr_t ip) {
    char* ipStringBuffer = g_malloc0(INET6_ADDRSTRLEN + 1);
    struct in_addr addr = {.s_addr = ip};
    const char* ipString = inet_ntop(AF_INET, &addr, ipStringBuffer, INET6_ADDRSTRLEN);
    GString* result = ipString ? g_string_new(ipString) : g_string_new("NULL");
    g_free(ipStringBuffer);
    return g_string_free(result, FALSE);
}
