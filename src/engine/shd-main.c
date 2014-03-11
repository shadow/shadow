/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

static Master* shadowMaster;

gint shadow_main(gint argc, gchar* argv[]) {
    /* check the compiled GLib version */
    if (!GLIB_CHECK_VERSION(2, 32, 0)) {
	    g_printerr("** GLib version 2.32.0 or above is required but Shadow was compiled against version %u.%u.%u\n",
		    (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
	    return -1;
    }

    /* check the that run-time GLib matches the compiled version */
    const gchar* mismatch = glib_check_version(GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
    if(mismatch) {
	    g_printerr("** The version of the run-time GLib library (%u.%u.%u) is not compatible with the version against which Shadow was compiled (%u.%u.%u). GLib message: '%s'\n",
        glib_major_version, glib_minor_version, glib_micro_version,
        (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
        mismatch);
	    return -1;
    }

	/* setup configuration - this fails and aborts if invalid */
	Configuration* config = configuration_new(argc, argv);
	if(!config) {
		/* incorrect options given */
		return -1;
	} else if(config->printSoftwareVersion) {
		g_printerr("Shadow v%s\nFor more information, visit http://shadow.github.io or https://github.com/shadow\n", SHADOW_VERSION);
		configuration_free(config);
		return 0;
	}

	/* we better have preloaded libshadow_preload.so */
	const gchar* ldPreloadValue = g_getenv("LD_PRELOAD");
	if(!ldPreloadValue || !g_strstr_len(ldPreloadValue, -1, "libshadow-preload.so")) {
		g_printerr("** Environment Check Failed: LD_PRELOAD does not contain libshadow-preload.so\n");
		return -1;
	}

	/* allocate and initialize our main simulation driver */
	shadowMaster = master_new(config);
	if(shadowMaster) {
		/* run the simulation */
		master_run(shadowMaster);
		/* cleanup */
		master_free(shadowMaster);
		shadowMaster = NULL;
	}

	configuration_free(config);

	return 0;
}
