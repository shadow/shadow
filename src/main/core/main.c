/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "main/core/main.h"

#include <errno.h>
#include <glib.h>
#include <sys/prctl.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"

int main_checkGlibVersion() {
    /* Technically redundant, since our minimum glib version enforced by cmake is already
     * larger than this version. Still, doesn't hurt to keep this check for posterity in
     * case we ever try to go back to supporting older versions.*/
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

void main_printBuildInfo(const ShadowBuildInfo* shadowBuildInfo) {
    g_printerr("Shadow %s running GLib v%u.%u.%u\n%s\n%s\n", shadowBuildInfo->version,
               (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
               shadowBuildInfo->build, shadowBuildInfo->info);
}

void main_logBuildInfo(const ShadowBuildInfo* shadowBuildInfo) {
    gchar* startupStr = g_strdup_printf("Starting Shadow %s with GLib v%u.%u.%u",
                                        shadowBuildInfo->version, (guint)GLIB_MAJOR_VERSION,
                                        (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);

    info("%s", startupStr);
    g_printerr("** %s\n", startupStr);
    g_free(startupStr);

    info("%s", shadowBuildInfo->build);
    info("%s", shadowBuildInfo->info);
    info("Logging current startup arguments and environment");
}
