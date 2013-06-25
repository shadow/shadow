/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_MAIN_H_
#define SHD_MAIN_H_

#include <glib.h>

/**
 * Main entry point for the simulator. Initializes logging, configuration,
 * allocates initial memory structures, launches thread pool, runs simulation.
 *
 * @param argc argument count
 * @param argv argument vector
 *
 * @returns an integer return code
 */
gint shadow_main(gint argc, gchar* argv[]);

#endif /* SHD_MAIN_H_ */
