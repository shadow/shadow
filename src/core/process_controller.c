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
#include "netconst.h"
#include "sysconfig.h"
#include "process.h"

/* Processes the communications between this DVN and some controller connection */
gint dvn_controller_process_msg (dvninstance_tp dvn, gint command, nbdf_tp nb) {
	switch(command) {
//		case NETCTL_CMD_START: {
//			gchar * dsim;
//
//			dlogf(LOG_INFO, "Master: Controller socket issued simulation start command. Loading DSIM and starting simulation.\n");
//
//			nbdf_read(nb, "S", &dsim);
//			inst->sim_master = sim_master_create(dsim,inst->workers_numactive);
//			free(dsim);
//
//			if(inst->sim_master == NULL) {
//				dlogf(LOG_ERR,"Master: DSIM file submitted by controller was invalid. \n");
//
//				return 1;
//			} else
//				inst->sim_state |= DS_SIMSTATE_REALTIME;
//
//			debugf( "Master: Attempting to start workers...\n");
//
//			/* now, create all the workers... */
//			for(i=0; i<inst->workers_length; i++) {
//				if(!(inst->workers[i].state & DS_WOSTATE_ACTIVE))
//					continue;
//
//				if(inst->workers[i].state & DS_WOSTATE_LOCAL) {
//					inst->sim_worker = sim_worker_create( i, inst->workers_numactive );
//					inst->workers[i].sim_worker = inst->sim_worker;
//
//				} else if(inst->workers[i].state & DS_WOSTATE_REMOTE) {
//					nbdf_tp start_nb = nbdf_construct("i", inst->workers_numactive);
//					debugf( "Master: Sending start command to worker %i\n", i);
//					dvn_write_simpacket(inst->workers[i].sock, NETSIM_CMD_START, start_nb);
//					nbdf_free(start_nb);
//				}
//			}
//			break;
//		}
//		case NETCTL_CMD_CONNECT:{
//			gchar * host; gint port;
//			socket_tp new_socket;
//
//			nbdf_read(nb, "Si",&host,&port);
//
//			new_socket = socket_create(SOCKET_OPTION_TCP|SOCKET_OPTION_NONBLOCK);
//			debugf( "Master: Connecting to remote controller-requested host...\n");
//
//			if(!socket_connect(new_socket, host, port)) {
//				dlogf(LOG_ERR, "Master: Unable to connect to controller-requested host '%s' on port %i.\n", host, port);
//			} else {
//				nbdf_tp nb_boot, nb_eip;
//				gint new_worker_id = inst->workers_numactive++;
//
//				dlogf(LOG_INFO, "Master: Connected to controller-requested host '%s' on port %i. Socket %i\n", host, port, socket_getfd(new_socket));
//
//				/* track the socket as a worker. */
//				dvn_workers_ensure(inst, new_worker_id + 1);
//				inst->workers[new_worker_id].state = DS_WOSTATE_ACTIVE|DS_WOSTATE_REMOTE;
//				inst->workers[new_worker_id].sim_worker = NULL;
//				inst->workers[new_worker_id].sock = new_socket;
//				inst->workers[new_worker_id].id = new_worker_id;
//
//				socketset_watch(inst->socketset, new_socket);
//
//				/* send a bootstrap */
//				nb_boot = nbdf_construct("ii", new_worker_id, 0);
//				dvn_write_simpacket(new_socket, NETSIM_CMD_BOOTSTRAP, nb_boot);
//				nbdf_free(nb_boot);
//
//				/* then tell him about all the other workers. */
//				for(i=0; i<inst->workers_numactive; i++) {
//					if(inst->workers[i].state & DS_WOSTATE_REMOTE && i!=new_worker_id) {
//						nb_eip = nbdf_construct("isi", i, socket_gethost(inst->workers[i].sock), socket_getport(inst->workers[i].sock));
//						debugf( "Master: Sending ENGAGEIP to new socket: %s %d\n",socket_gethost(inst->workers[i].sock),socket_getport(inst->workers[i].sock) );
//						dvn_write_simpacket(new_socket, NETSIM_CMD_ENGAGEIP, nb_eip);
//						nbdf_free(nb_eip);
//					}
//				}
//			}
//
//			free(host);
//
//			break;
//		}

		case DVN_CFRAME_START:
			break;
		case DVN_CFRAME_CONNECT:
			break;
		case DVN_CFRAME_CONFIG: {
			gchar * config;

			nbdf_read(nb, "S",&config);
			if(!config)
				break;

			sysconfig_import_config(config);
			dlogf(LOG_MSG, "Loaded and merged new instance configuration data.\n");

			free(config);

			break;
		}

		case DVN_CFRAME_GETCONFIG: {
			/*gchar * config = sysconfig_export_config();
			nbdf_tp nb = nbdf_

			free(config);*/

			break;
		}

		case DVN_CFRAME_SHUTDOWN:
			dlogf(LOG_MSG, "Master: Forced Shutdown from Controller\n");
			dvn->ending = 1;
			break;

		/* TODO: here would go all the various queries, stat collection from a controller socket */
		default:
			break;
	}
	return 1;
}

/* Process communications between this DVN and a controller */
gint dvn_controller_process (dvninstance_tp dvn, socket_tp sock) {
	gint rv = 1;
	do {
		nbdf_tp nb = NULL, frame_nb = NULL;
		gint prefix, command;

		if(nbdf_frame_avail(sock)) {
			nb = nbdf_import_frame(sock);
			nbdf_read(nb, "iin", &prefix, &command, &frame_nb);

			if(prefix != DVN_CPREFIX)
				rv = 0;
			else
				debugf( "Master: dvn_controller_process: Got a valid NBDF frame.\n");

			nbdf_free(nb); nb = NULL;
		}

		if(frame_nb != NULL) {
			if(!dvn_controller_process_msg(dvn, command, frame_nb))
				rv = 0;

			free(frame_nb);

		} else
			break;

	} while(rv);

	return rv;
}
