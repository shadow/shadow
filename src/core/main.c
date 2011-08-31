/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "global.h"
#include "log.h"
#include "sysconfig.h"
#include "process.h"
#include "clo.h"
#include "socket.h"
#include "rand.h"


/*
 * command line options:
 * 	-p	--controller-port <port>
 * 		sets the port the controller socket listens on
 *  -a  --controller-address <ip>
 * 		sets the address the controller socket binds to.
 * 		if unspecified, will bind to all available addresses
 *  -d  --daemonize
 * 		runs the server as a daemon
 *  -l  --logfile <path to logfile>
 * 		specifies the server log file
 *  -t  --threads
 * 		specifies the total number of threads to use.
 * 		must be at least one.
 */

#define DOPTION_MASTER        1
#define DOPTION_SLAVE         2
#define DOPTION_NORMAL        3

#define DOPTION_PORT          4
#define DOPTION_CPORT         5

#define DOPTION_DSIM          6
#define DOPTION_PROCS         7


#define DOPTION_CONFIG        8
#define DOPTION_CONFIGDUMP    9
#define DOPTION_HELP          10
#define DOPTION_VERSION       11

//#define DOPTION_BACKGROUND    12

#define DOPTION_LOG0			100
#define DOPTION_LOG1			101
#define DOPTION_LOG2			102
#define DOPTION_LOG3			103

gint clo_handle(gchar *v, gint o, gpointer arg) {
	struct DVN_CONFIG* dconfig = (struct DVN_CONFIG*)arg;

	switch(o) {
		/* ------ mode --------*/
		case DOPTION_NORMAL:
			dconfig->dvn_mode = dvn_mode_normal;
			break;

		case DOPTION_MASTER:
			dconfig->dvn_mode = dvn_mode_slave;
			break;

		case DOPTION_SLAVE:
			dconfig->dvn_mode = dvn_mode_slave;
			break;


		/* ------ ports --------*/
		case DOPTION_PORT:
			dconfig->slave_listen_port = atoi(v);
			break;

		case DOPTION_CPORT:
			dconfig->controller_listen_port = atoi(v);
			break;


		/* ------ config --------*/
		case DOPTION_CONFIG:
			strncpy(dconfig->config_file, v, sizeof(dconfig->config_file));
			dconfig->config_file[sizeof(dconfig->config_file)-1]=0;
			break;

		case DOPTION_CONFIGDUMP:
			dconfig->config_dump = 1;
			break;

		/* ------ processes --------*/
		case DOPTION_PROCS:
			dconfig->num_processes = atoi(v);
			if(dconfig->num_processes < 1)
				return CLO_BAD;
			break;


		/* ------ dsim --------*/
		case DOPTION_DSIM:
			strncpy(dconfig->dsim_file, v, sizeof(dconfig->dsim_file));
			dconfig->dsim_file[sizeof(dconfig->dsim_file)-1]=0;
			break;

		/* ------- help/version -----*/
		case DOPTION_HELP:
			return CLO_USAGE;

		case DOPTION_VERSION:
			dconfig->version = 1;
			break;


		/*case DOPTION_BACKGROUND:
			dconfig->background = 1;
			break;*/

		/* ------- logs --------*/
		case DOPTION_LOG0:
		case DOPTION_LOG1:
		case DOPTION_LOG2:
		case DOPTION_LOG3: {
			gint logchannel = o-DOPTION_LOG0;
			strncpy(dconfig->log_destinations[logchannel], v, sizeof(dconfig->log_destinations[0]));
			dconfig->log_destinations[logchannel][sizeof(dconfig->log_destinations[0])-1] = 0;
			break;
		}

		default:
			return CLO_BAD;
	}
	return CLO_OKAY;
}

struct CLO_entry cloentries[] = {
		{DOPTION_MASTER, 	'm',"--master",0,"Enables daemon mode: sets this machine to be a master node."},
		{DOPTION_SLAVE, 	's',"--slave",0,"Enables daemon mode: sets this machine to be a slave node."},
		{DOPTION_NORMAL, 	'n',"--normal",0,"Enables non-daemon mode: load and execute specified DSIM file. (default)"},

		//{DOPTION_BACKGROUND,	'b', "--background",0,"Detaches DVN from foreground."},

		{DOPTION_PROCS,		'p',"--processes",1,"Sets the number of worker processes DVN should use. (default/min: 1)"},
		{DOPTION_DSIM, 		'd', "--dsim", 1, "Sets the DSIM file to load and run."},

		{DOPTION_CPORT, 	0,"--controller-port",1,"(master mode) Port to listen on for controller socket. (default: 6200)"},
		{DOPTION_PORT, 		0,"--port",1,"Port to listen on for worker sockets. (default: 6201)"},

		{DOPTION_CONFIG, 	'c',"--config",1,"Specifies DVN configuration file to load."},
		{DOPTION_HELP, 		'h',"--help",0,"Help"},
		{DOPTION_VERSION, 	'v', "--version",0,"Display DVN version and exit"},
		{DOPTION_CONFIGDUMP, 0, "--config-dump",0,"Dumps the DVN runtime configuration (loadable using -c)"},

		{DOPTION_LOG0, 		0,"--log0",1,"Destination for DVN log channel 0. ('socket:<host>:<port>','file:<path>','stdout','null') (Daemon monde only)"},
		{DOPTION_LOG1, 		0,"--log1",1,"Destination for DVN log channel 1. (Farm Mode Only)"},
		{DOPTION_LOG2, 		0,"--log2",1,"Destination for DVN log channel 2. (Farm Mode Only)"},
		{DOPTION_LOG3, 		0,"--log3",1,"Destination for DVN log channel 3. (Farm Mode Only)"},

		{.id=0}
	 };

/* entry for dvn */
gint main (gint argc, gchar * argv[]) {
	struct DVN_CONFIG config;

	/* setup system defaults */
	config.controller_listen_port = 6200;
	config.slave_listen_port = 6201;

	config.config_dump = 0;
	config.dvn_mode = dvn_mode_normal;

	config.num_processes = 1;
	config.background = 0;

	config.version = 0;
	config.config_file[0] = 0;
	config.dsim_file[0] = 0;
	memset(config.log_destinations, 0, sizeof(config.log_destinations));

	/* parse command-line-args */
	if(!parse_clo(argc,argv,cloentries,clo_handle,&config))
		exit(1);

	if(config.version) {
		printf(PACKAGE_STRING " (c) 2006-2009 Tyson Malchow\n");
		exit(0);
	}

	// TEMPORARY
	if(config.dvn_mode != dvn_mode_normal)
		printfault(EXIT_UNKNOWN, "DVN currently only supports NORMAL mode processing (no daemon yet available).");

	/* init systemwide configuration */
	sysconfig_init();

	/* init logging system */
#ifdef DEBUG
	dlog_init("debug");
#else
	dlog_init(sysconfig_get_string("loglevel"));
#endif

	/* init the randomization vectors... */
	dvn_rand_seed(1);

	/* socket init */
	socket_ignore_sigpipe();

	/* disable mmaping malloc()s
	mallopt(M_MMAP_MAX, 0);*/

	//socket_enable_async();

	/* process configuration */
	if(config.config_dump) {
		printf("%s\n", sysconfig_export_config());
		exit(0);
	} else if(config.config_file[0]) {
		gchar * configuration = file_get_contents(config.config_file);
		if(!configuration)
			printfault(EXIT_FAILURE, "Unable to open configuration file '%s'\n", config.config_file);
		sysconfig_import_config(configuration);
		free(configuration);

		dlogf(LOG_MSG, "Configuration from '%s' merged OK.\n", config.config_file);
	}

	if(config.dvn_mode == dvn_mode_normal && !config.dsim_file[0])
		printfault(EXIT_FAILURE, "You must specify a DSIM file to load when using DVN outside of daemon mode.\n");

	debugf("Core: DVN Starting\n");

	/*if(config.background) {
		debugf("Core: Detaching ginto daemon\n");

	to daemonize, i need to ....
	 * fork()
	 * umask(0)
	 * setsid()
	 * close the stdio FDs
	 * open logs

	}*/

	dvn_main(&config);

	sysconfig_cleanup();
	dlog_cleanup();



	return 0;
}

