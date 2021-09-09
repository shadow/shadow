/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <glib.h>
#include <sys/prctl.h>

#include "lib/logger/logger.h"
#include "shd-config.h"

bool main_sidechannelMitigationsEnabled() {
    int state = prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, 0, 0, 0);
    if (state == -1) {
        panic("prctl: %s", g_strerror(errno));
    }
    return (state & PR_SPEC_DISABLE) != 0;
}

int main_checkGlibVersion() {
    /* check the compiled GLib version */
    if (!GLIB_CHECK_VERSION(2, 32, 0)) {
        g_printerr("** GLib version 2.32.0 or above is required but Shadow was compiled against "
                   "version %u.%u.%u\n",
                   (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return -1;
    }

    if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION == 40) {
        g_printerr("** You compiled against GLib version %u.%u.%u, which has bugs known to break "
                   "Shadow. Please update to a newer version of GLib.\n",
                   (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return -1;
    }

    /* check the that run-time GLib matches the compiled version */
    const gchar* mismatch =
        glib_check_version(GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
    if (mismatch) {
        g_printerr("** The version of the run-time GLib library (%u.%u.%u) is not compatible with "
                   "the version against which Shadow was compiled (%u.%u.%u). GLib message: '%s'\n",
                   glib_major_version, glib_minor_version, glib_micro_version,
                   (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
                   mismatch);
        return -1;
    }

    return 0;
}

void main_printBuildInfo() {
    g_printerr("Shadow %s running GLib v%u.%u.%u\n%s\n%s\n", SHADOW_VERSION_STRING,
               (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
               SHADOW_BUILD_STRING, SHADOW_INFO_STRING);
}

void main_logBuildInfo(char** argv) {
    gchar* startupStr = g_strdup_printf("Starting Shadow %s with GLib v%u.%u.%u",
                                        SHADOW_VERSION_STRING, (guint)GLIB_MAJOR_VERSION,
                                        (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);

    info("%s", startupStr);
    g_printerr("** %s\n", startupStr);
    g_free(startupStr);

    info("%s", SHADOW_BUILD_STRING);
    info("%s", SHADOW_INFO_STRING);
    info("Logging current startup arguments and environment");
}
