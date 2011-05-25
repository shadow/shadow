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

#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "global.h"
#include "process.h"
#include "pipecloud.h"
#include "sim.h"
#include "nbdf.h"
#include "vector.h"
#include "sysconfig.h"
#include "netconst.h"
#include "routing.h"
#include "log.h"

dvninstance_tp dvn_global_instance = NULL;
dvn_global_worker_data_t dvn_global_worker_data = {0, NULL, 0, 0};

int dvn_worker_main (unsigned int process_id, unsigned int total_workers, pipecloud_tp pipecloud) {
	nbdf_tp pipe_frame;
	int run = 1;
	sim_worker_tp worker = NULL;
	size_t num_event_worker_executed = 0;
	
	debugf("Worker: %d has Started.\n", process_id);

	/* configure global datastructure with worker information */
	dvn_global_worker_data.in_worker = 1;
	dvn_global_worker_data.pipecloud = pipecloud;
	dvn_global_worker_data.process_id = process_id;
	dvn_global_worker_data.total_workers = total_workers;

	/* prep for simulation */
	//context_init();
	pipecloud_config_localized(pipecloud, process_id);

	/* set dlog to pipecloud mode: all logging routed to master process for handling */
	dlog_set_dvn_routing(1);
	

	//dlog_set_prefix();
	while(run) {
		/* we can only block if the worker was idle. otherwise it is expecting us to
		 * read more events and it needs to send out a state change with the next event time. */
		if(num_event_worker_executed == 0) {
			pipecloud_select(pipecloud, PIPECLOUD_MODE_BLOCK);
		} else {
			pipecloud_select(pipecloud, PIPECLOUD_MODE_POLL);
		}

		// process all frames from pipecloud
		while((pipe_frame = nbdf_import_frame_pipecloud(pipecloud)) != NULL) {
			unsigned char dest_type, dest_layer;
			unsigned int dest_major, frametype;
			nbdf_tp frame;

			nbdf_read(pipe_frame, "cciin", &dest_type, &dest_layer, &dest_major, &frametype, &frame);

			if(dest_layer & DVNPACKET_LAYER_PRC) {
				switch(frametype) {
					case DVN_FRAME_STARTSIM: {
						int num_slaves, max_wrkrs, slave_id;
						nbdf_read(frame, "iii", &slave_id, &num_slaves, &max_wrkrs);

						worker = sim_worker_create(pipecloud, slave_id, process_id, num_slaves, total_workers, max_wrkrs);
						context_set_worker(worker);
						debugf("Worker: Sim worker created (%d)\n", process_id);

						break;
					}

					case DVN_FRAME_DIE:
						run = 0;
						break;

				}
			} else if(worker) {
				/* destined for sim layer... */
				sim_worker_deposit(worker, frametype, frame);
			}

			nbdf_free(pipe_frame);
			nbdf_free(frame);
		}

		/*
		 * run simulation processing - this will run til the current IO state says it can't anymore. in the meantime,
		 * other processes will hopefully have sent us, over the pipecloud, the data we need to continue running.
		 */
		if(worker) {
			num_event_worker_executed = 0;
			if(sim_worker_heartbeat(worker, &num_event_worker_executed) < 0) {
				sim_worker_destroy(worker);
				worker = NULL;
			}
		}
	}

	if(worker){
		sim_worker_destroy(worker);
		worker = NULL;
	}

	debugf("Worker: clean exit.\n");

	return 0;
}


void dvn_master_heartbeat (dvninstance_tp dvn) {

	/* check for new controller connections */
	if(dvn->master->controller_sock && socketset_is_readset(dvn->socketset, dvn->master->controller_sock)) {
		socket_tp new_socket = socket_create_child(dvn->master->controller_sock, SOCKET_OPTION_NONBLOCK);

		if(new_socket) {
			if(socket_isvalid(new_socket)) {
				vector_push(dvn->master->controller_sockets, new_socket);
				socketset_watch(dvn->socketset, new_socket);

				dlogf( LOG_MSG,"Accepted a new controller socket (%i).\n",socket_getfd(new_socket));
			} else {
				socket_destroy(new_socket);
			}
		}
	}

	/* check if there's waiting data on any controller socket */
	for(unsigned int i = 0; i < vector_size(dvn->master->controller_sockets); i++ ) {
		socket_tp cur_socket = vector_get(dvn->master->controller_sockets, i);

		if( 	!socket_isvalid(cur_socket) ||
				(socket_data_incoming(cur_socket) && !dvn_controller_process(dvn, cur_socket))) {

			vector_remove(dvn->master->controller_sockets, i--);
			socket_destroy(cur_socket);
		}
	}

	return;
}

void dvn_slave_deposit (dvninstance_tp dvn, nbdf_tp net_frame) {
	unsigned char dest_type, dest_layer;
	int dest_major, frametype;
	nbdf_tp frame;

	nbdf_read(net_frame, "cciin", &dest_type, &dest_layer, &dest_major, &frametype, &frame);

	/* sim delivery... */
	if(dest_layer & DVNPACKET_LAYER_SIM) {
		if(dest_type == DVNPACKET_MASTER)
			sim_master_deposit(dvn->master->sim_master, frametype, frame);
		else
			sim_slave_deposit(dvn->slave->sim_slave, frametype, frame);

	} else {
		switch(frametype) {
			case DVN_FRAME_ENGAGEIP: {
				int port, slave_id;
				char * host;
				socket_tp new_socket = socket_create(SOCKET_OPTION_TCP|SOCKET_OPTION_NONBLOCK);

				nbdf_read(frame, "iSi", &slave_id, &host, &port);
				debugf( "Slave: Engaging remote host ID%d: %s %i...\n", slave_id, host, port);

				if(!socket_connect(new_socket, host, port)) {
					dlogf(LOG_ERR,"Slave:     Unable to connect. Bad.\n");
					socket_destroy(new_socket);
				} else {
					dvninstance_slave_connection_tp new_slave_connection;
					nbdf_tp iden_nb;

					/* we know this worker's ID already, so we track them, then identify ourselves to them. */
					new_slave_connection = malloc(sizeof(*new_slave_connection));
					if(!new_slave_connection)
						printfault(EXIT_NOMEM, "dvn_slave_process: Out of memory");

					new_slave_connection->sock = new_socket;
					new_slave_connection->id = slave_id;
					socketset_watch(dvn->socketset, new_socket);

					vector_push(dvn->slave->slave_connections, new_slave_connection);
					g_hash_table_insert(dvn->slave->slave_connection_lookup, int_key(slave_id), new_slave_connection);
					dvn->num_active_slaves++;

					debugf( "Slave:     Engaged. Now %d active slaves.\n", dvn->num_active_slaves);

					iden_nb = nbdf_construct("i", dvn->my_instid);
					dvn_packet_write(new_socket, DVNPACKET_SLAVE, DVNPACKET_LAYER_PRC, slave_id, DVN_FRAME_IDENTIFY, iden_nb);
					nbdf_free(iden_nb);
				}

				free(host);
				break;
			}
		}
	}

	nbdf_free(frame);
	return;
}

/**
 * processes incoming data from remote slaves
 */
int dvn_slave_socketprocess(dvninstance_tp dvn, dvninstance_slave_connection_tp slave_connection) {
	int rv = 1;

	while(rv == 1) {
		unsigned char dest_type, dest_layer;
		unsigned int dest_major, frametype;
		nbdf_tp frame = NULL, net_frame = NULL;

		if(!nbdf_frame_avail(slave_connection->sock))
			break;

		net_frame = nbdf_import_frame(slave_connection->sock);
		nbdf_read(net_frame, "cciin", &dest_type, &dest_layer, &dest_major, &frametype, &frame);

		if(slave_connection->id == -1) {
			if( frametype == DVN_FRAME_BOOTSTRAP ) {
				int assigned_id;
				nbdf_read(frame, "i", &assigned_id);

				/* set our own ID */
				dvn->my_instid = assigned_id;

                                int key = 0;
				if(g_hash_table_lookup(dvn->slave->slave_connection_lookup, &key) == NULL) {
					/* mark this link as ID 0 (master) */
					slave_connection->id = 0;
					g_hash_table_insert(dvn->slave->slave_connection_lookup, int_key(key), slave_connection);
					dvn->num_active_slaves++;

					debugf( "Slave: BOOTSTRAPED to ID %i (master connection established)\n",assigned_id);
				}

			} else if(frametype == DVN_FRAME_IDENTIFY) {
				int id;

				nbdf_read(frame, "i", &id);

				if(g_hash_table_lookup(dvn->slave->slave_connection_lookup, &id)) {
					debugf("Slave: Got a identification for worker %i\n",id);
					slave_connection->id = id;
					g_hash_table_insert(dvn->slave->slave_connection_lookup, int_key(id), slave_connection);
				}
			}
		} else {
			dvn_slave_deposit(dvn, net_frame);
		}

		nbdf_free(frame);
		nbdf_free(net_frame);
	}

//			else if(command == NETSIM_CMD_NODELOG && inst->sim_master) {
//				/*dlogf("REMOTE: %s\n", LOG_NODE, framebuf);*/
//
//			} else if(command == NETSIM_CMD_MASTERDELIV && inst->sim_master) {
//				/* delivery for the master. */
//				debugf("dvn_worker_process: Master depositing\n");
//				sim_master_deposit(inst->sim_master, worker->id, frame_nb);
//
//			} else if( command == NETSIM_CMD_WORKERDELIV && inst->sim_worker ) {
//				/* delivery for our worker */
//				sim_worker_deposit(inst->sim_worker, worker->id, frame_nb);
//
//			} else if( command == NETSIM_CMD_START) {
//				/* starting a new simulation... all other workers should be connected right now. */
//				int i;
//
//				debugf("Got NETSIM_CMD_START (numactive: %d)\n", inst->workers_numactive);
//
//
//				/* find our local worker and create the simulation logic data instance */
//				for(i=0; i<inst->workers_length; i++) {
//					if(inst->workers[i].state & DS_WOSTATE_LOCAL && inst->workers[i].state & DS_WOSTATE_ACTIVE) {
//						inst->workers[i].sim_worker = sim_worker_create(i,inst->workers_numactive);
//						inst->sim_worker = inst->workers[i].sim_worker;
//						break;
//					}
//				}
//
//			} 	/* otherwise, ignore the fucker*/
//
//			nbdf_free(frame_nb);
//		} else if( command == NETSIM_CMD_SHUTDOWN ) {
//			dlogf(LOG_INFO, "Master notification to shutdown.\n");
//			inst->sim_state |= DS_SIMSTATE_ENDING | DS_SIMSTATE_REALTIME;
//			rv = 0;
//		} else
//			break;


	return rv;
}

void dvn_slave_heartbeat (dvninstance_tp dvn) {
	nbdf_tp frame;

	if(dvn->slave->slave_sock) { /* check for new slave connections */
		if(socketset_is_readset(dvn->socketset, dvn->slave->slave_sock)) {
			socket_tp new_socket = socket_create_child(dvn->slave->slave_sock, SOCKET_OPTION_NONBLOCK);

			if(new_socket) {
				if(socket_isvalid(new_socket)) {
					dvninstance_slave_connection_tp slc = malloc(sizeof(*slc));

					slc->id = -1;
					slc->sock = new_socket;

					vector_push(dvn->slave->slave_connections, slc);
					socketset_watch(dvn->socketset, new_socket);

					dlogf( LOG_MSG,"Accepted a new slave connection (%i).\n",socket_getfd(new_socket));

				} else
					socket_destroy(new_socket);
			}
		}
	}

	/* check if there's waiting data on any slave socket */
	for(unsigned int i = 0; i < vector_size(dvn->slave->slave_connections); i++ ) {
		dvninstance_slave_connection_tp cur_slave = vector_get(dvn->slave->slave_connections, i);

		if( 	!cur_slave || !socket_isvalid(cur_slave->sock) ||
				(socket_data_incoming(cur_slave->sock) && !dvn_slave_socketprocess(dvn, cur_slave))) {

			vector_remove(dvn->slave->slave_connections, i--);

			/* DESTROY */
			if(cur_slave) {
				if(cur_slave->id != -1)
					g_hash_table_remove(dvn->slave->slave_connection_lookup, &cur_slave->id);

				if(cur_slave->sock)
					socket_destroy(cur_slave->sock);

				free(cur_slave);
			}
		}
	}

	/* finally, check the pipecloud for any waiting data - we have to run
	 * routing logic here, because workers will forward us packets for which we
	 * are not the [only] final destination */
	pipecloud_select(dvn->slave->pipecloud, PIPECLOUD_MODE_POLL);
	while((frame = nbdf_import_frame_pipecloud(dvn->slave->pipecloud)) != NULL) {
		unsigned char dest_type, dest_layer;
		int dest_major, frametype;

		nbdf_read(frame, "ccii", &dest_type, &dest_layer, &dest_major, &frametype); // should be "cciin" but we don't need to look at the payload (faster)

		switch(dest_type) {
			case DVNPACKET_WORKER:
			case DVNPACKET_WORKER_BCAST: /* ignore - worker dests have no purpose being here */
				break;

			case DVNPACKET_GLOBAL_BCAST: {
				/* send out remote slaves */
				for(int i=0; i<vector_size(dvn->slave->slave_connections); i++) {
					dvninstance_slave_connection_tp remote_slave_connection =
							vector_get(dvn->slave->slave_connections, i);

					if(!remote_slave_connection->sock || remote_slave_connection->id < 0)
						continue;

					/* write to socket (always nonblocking) */
					nbdf_send(frame, remote_slave_connection->sock);
				}

				/* our slave is also an endpoint. */
				dvn_slave_deposit(dvn, frame);

				break;
			}

			case DVNPACKET_LOCAL_SLAVE:
			case DVNPACKET_LOCAL_BCAST: { /* it was a LOCAL broadcast, which means our local slave was an endpoint */
				dvn_slave_deposit(dvn, frame);
				break;
			}
			
			case DVNPACKET_LOG: {
				nbdf_tp log_frame;
				nbdf_read(frame, "cciin",  &dest_type, &dest_layer, &dest_major, &frametype, &log_frame);
				dlog_deposit(frametype, log_frame);
				nbdf_free(log_frame);
				break;
			}

			case DVNPACKET_SLAVE: { /* send to proper slave */
				if(dvn->my_instid == dest_major)
					dvn_slave_deposit(dvn, frame);

				else {
					dvninstance_slave_connection_tp remote_slave_connection =
						g_hash_table_lookup(dvn->slave->slave_connection_lookup, &dest_major);

					if(remote_slave_connection && remote_slave_connection->sock)
						nbdf_send(frame, remote_slave_connection->sock);
				}
				break;
			}

			case DVNPACKET_MASTER: { /* get it to master */
				if(dvn->my_instid == 0)
					dvn_slave_deposit(dvn, frame);

				else {
                                        int key = 0;
					dvninstance_slave_connection_tp remote_slave_connection =
						g_hash_table_lookup(dvn->slave->slave_connection_lookup, &key);

					if(remote_slave_connection && remote_slave_connection->sock)
						nbdf_send(frame, remote_slave_connection->sock);
				}
				break;
			}
		}

		nbdf_free(frame);
	}



	return;
}


dvninstance_master_tp dvn_create_master (int is_daemon, unsigned int controller_port, socketset_tp socketset) {
	dvninstance_master_tp master = malloc(sizeof(*master));

	master->controller_sock = NULL;
	master->controller_sockets = vector_create();
	master->is_daemon_mode = is_daemon ? 1 : 0;
	master->sim_master = NULL;

	if(master->is_daemon_mode) {
		/* open the controller socket */
		master->controller_sock = socket_create(SOCKET_OPTION_NONBLOCK | SOCKET_OPTION_TCP);
		if(!socket_listen(master->controller_sock, controller_port, 3)) {
			int e = errno;
			dlogf(LOG_ERR, "dvn_create_master: Unable to open controller listen socket on %i. Aborting.\n%s\n", controller_port, strerror(e));
			socket_destroy(master->controller_sock);
			free(master);
			return NULL;
		}
	}

	return master;
}

void dvn_destroy_master (dvninstance_master_tp master) {
	if(!master)
		return;

	if(master->sim_master)
		sim_master_destroy(master->sim_master);

	if(master->controller_sock)
		socket_destroy(master->controller_sock);

	if(master->controller_sockets) {
		socket_tp csock;

		while((csock = vector_pop(master->controller_sockets)) != NULL)
			socket_destroy(csock);

		vector_destroy(master->controller_sockets);
	}

	free(master);
}

dvninstance_slave_tp dvn_create_slave (int daemon, unsigned int num_processes, unsigned int slave_listen_port, socketset_tp socketset) {
	dvninstance_slave_tp slave = malloc(sizeof(*slave));

	slave->slave_connection_lookup = NULL;
	slave->slave_connections = NULL;
	slave->slave_sock = NULL;
	slave->num_processes = num_processes;
	slave->sim_slave = NULL;

	/* create/attach the pipecloud */
	slave->pipecloud = pipecloud_create(num_processes + 1, (num_processes + 1) * sysconfig_get_int("pipecloud_pp_size"), 1);

	/* for saving PIDs */
	slave->worker_process_ids = malloc(sizeof(int) * num_processes);

	/* first, we're going to fork off all the worker processes */
	for(unsigned int i = 0; i < num_processes; i++) {
		int fork_result = fork();

		if(fork_result < 0)
			printfault(EXIT_UNKNOWN, "fork failed");

		/* the worker process should run only dvn_worker_main then exit */
		else if(fork_result == 0) {
			/* we will assume COW pages after FORK, so the less we touch, the less we use. don't clean up anything. */
			exit (dvn_worker_main (i+1, num_processes, slave->pipecloud));
		}

		else
			slave->worker_process_ids[i] = fork_result;
	}

	/* init pipecloud as ID 0 and enable signal notifications */
	pipecloud_config_localized(slave->pipecloud, 0);

	/* create data structures for tracking remote connections */
	slave->slave_connection_lookup = g_hash_table_new(g_int_hash, g_int_equal);
	slave->slave_connections = vector_create();

	/* if in daemon mode, create network slave socket */
	if(daemon) {
		slave->slave_sock = socket_create (SOCKET_OPTION_NONBLOCK | SOCKET_OPTION_TCP);

		if(!socket_isvalid(slave->slave_sock) || !socket_listen(slave->slave_sock, slave_listen_port, 3)) {
			int e = errno;
			dlogf(LOG_ERR,"dvn_create_slave: Unable to open slave listen socket on port %i. Aborting.\n%s\n", slave_listen_port,strerror(e));
			free(slave);
			return NULL;
		}

		if(socketset)
			socketset_watch (socketset, slave->slave_sock);
	}

	if(socketset) 
		socketset_watch_readfd (socketset, pipecloud_get_wakeup_fd(slave->pipecloud));

	return slave;
}

void dvn_destroy_slave (dvninstance_slave_tp slave) {
	nbdf_tp die_frame;

	if(!slave)
		return;

	/* destroy the simulation slave, if present */
	if(slave->sim_slave)
		sim_slave_destroy(slave->sim_slave);

	/* notify all worker processes to die */
	die_frame = nbdf_construct("i", 0);
	dvn_packet_route(DVNPACKET_WORKER_BCAST, DVNPACKET_LAYER_PRC, 0, DVN_FRAME_DIE, die_frame);
	nbdf_free(die_frame);

	/* wait for them to die */
	for(unsigned int i=0; i < slave->num_processes; i++) {
		int status;
		waitpid(slave->worker_process_ids[i], &status, WUNTRACED);
	}

	/* clean ! */
	free(slave->worker_process_ids);

	if(slave->pipecloud)
		pipecloud_destroy(slave->pipecloud);

	if(slave->slave_sock)
		socket_destroy(slave->slave_sock);

	if(slave->slave_connections) {
		for(unsigned int i = 0; i < vector_size(slave->slave_connections); i++) {
			dvninstance_slave_connection_tp cur_slave = vector_get(slave->slave_connections, i);
			if(cur_slave) {
				if(cur_slave->sock)
					socket_destroy(cur_slave->sock);
				free(cur_slave);
			}
		}
		vector_destroy(slave->slave_connections);
	}

	if(slave->slave_connection_lookup)
		g_hash_table_destroy(slave->slave_connection_lookup);

	free(slave);
}

dvninstance_tp dvn_create_instance (struct DVN_CONFIG * config) {
	dvninstance_tp dvn = malloc(sizeof(*dvn));

	if(!dvn)
		printfault(EXIT_NOMEM, "dvn_create_instance: Out of memory");

	dvn->slave = NULL;
	dvn->master = NULL;
	dvn->socketset = socketset_create ();
	dvn->my_instid = -1;
	dvn->num_active_slaves = 1;

	/* all dvn instances have slave logic */
	dvn->slave = dvn_create_slave (config->dvn_mode != dvn_mode_normal ? 1 : 0,config->num_processes,config->slave_listen_port,dvn->socketset);

	/* we need master logic in master or normal mode */
	if(config->dvn_mode == dvn_mode_normal || config->dvn_mode == dvn_mode_master) {
		dvn->my_instid = 0;
		dvn->master = dvn_create_master (config->dvn_mode == dvn_mode_master, config->controller_listen_port, dvn->socketset);
	}

	dvn_global_instance = dvn;

	return dvn;
}

void dvn_destroy_instance (dvninstance_tp dvn) {
	if(!dvn)
		return;

	if(dvn->master)
		dvn_destroy_master(dvn->master);

	dvn_destroy_slave(dvn->slave);

	socketset_destroy(dvn->socketset);

	free(dvn);
}

int dvn_main (struct DVN_CONFIG * config) {
	dvninstance_tp dvn;

	if(!config)
		return 1;

	/* open logs */
	for(int i=0; i<(sizeof(config->log_destinations)/sizeof(config->log_destinations[0])); i++) {
		if(config->log_destinations[i][0] != '\0')
			dlog_set_channel(i, config->log_destinations[i], 0);
	}

	/* init (this will fork off worker processes) */
	dvn = dvn_create_instance(config);

	/* if we're in normal mode, we need to load up the DSIM file and start processing ASAP */
	if(config->dvn_mode == dvn_mode_normal) {
		char * dsim = file_get_contents(config->dsim_file);
		if(!dsim) {

		} else {
			/* issue a command to workers so they each create a sim_worker - they need this to accept
			 * OPs from the sim_master */
			nbdf_tp pf_start_nb = nbdf_construct("iii", 0, 1, sysconfig_get_int("max_workers_per_slave"));
			dvn_packet_route(DVNPACKET_WORKER_BCAST, DVNPACKET_LAYER_PRC, 0, DVN_FRAME_STARTSIM, pf_start_nb);
			nbdf_free(pf_start_nb);

			/* create the simulation slave */
			dvn->slave->sim_slave = sim_slave_create(0, dvn->slave->num_processes);

			/* create master - load DSIM and spool ops to workers */
			dvn->master->sim_master = sim_master_create(dsim, dvn->num_active_slaves);

			if(!dvn->master->sim_master) {
				dvn->ending = 1;

				/* issue destruction command to workers */
				//TODO: destruction issuance

				/* now, destroy simulation slave */
				sim_slave_destroy(dvn->slave->sim_slave);
				dvn->slave->sim_slave = NULL;
			} else
				dvn->ending = 0;

			free(dsim);
		}
	}

	/* dvn IO mainloop */
	while(!dvn->ending) {
		//debugf("Core: Tick.\n");
		socketset_update(dvn->socketset, NULL, 0);

		if(dvn->master)
			dvn_master_heartbeat (dvn);

		dvn_slave_heartbeat (dvn);

		if(dvn->master) {
			if(sim_master_isdone(dvn->master->sim_master))
				dvn->ending = 1;
		}
	}

	/* flush out all waiting socket writes */
	while(socketset_update(dvn->socketset, NULL, 1));

	/* cleanup */
	dvn_destroy_instance(dvn);

	debugf("Core: clean exit\n");

	return 0;
}


