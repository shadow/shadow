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

#include "global.h"
#include "process.h"
#include "routing.h"
#include "socket.h"
#include "nbdf.h"
#include "netconst.h"

extern dvn_global_worker_data_t dvn_global_worker_data;
extern dvninstance_tp dvn_global_instance;

void dvn_packet_write(socket_tp socket, unsigned char dest_type, unsigned char dest_layer, int dest_major, int frametype, nbdf_tp frame) {
	nbdf_tp net_nb = nbdf_construct("cciin", dest_type, dest_layer, dest_major,  frametype, frame);
	nbdf_send(net_nb, socket);
	nbdf_free(net_nb);
}

void dvn_packet_route(unsigned char dest_type, unsigned char dest_layer, int dest_major, int frametype, nbdf_tp frame) {
	/* if we are a worker and already reported complete, do not send out
	 * anything because there might not be anyone waiting to receive (avoids deadlocks)
	 * this cuts off logging at the end of the sim, but at this point master does
	 * not handle logging anymore anyway */
	if(dvn_global_worker_data.in_worker && global_sim_context.sim_worker &&
			global_sim_context.sim_worker->mode == sim_worker_mode_complete) {
		return;
	}

	nbdf_tp net_nb = nbdf_construct("cciin", dest_type, dest_layer, dest_major,  frametype, frame);

	if(dvn_global_worker_data.in_worker) { /* from worker context */
		switch(dest_type) {
			case DVNPACKET_WORKER_BCAST:{
				if(dest_layer & DVNPACKET_LAYER_OPT_DLOCAL) {
					/* would this ever happen? hopefully not. */
					for(int i=1; i <= dvn_global_worker_data.total_workers; i++)
						nbdf_send_pipecloud(net_nb, i, dvn_global_worker_data.pipecloud);
				} else {
					for(int i=1; i <= dvn_global_worker_data.total_workers; i++) {
						if(i == dvn_global_worker_data.process_id)
							continue;
						nbdf_send_pipecloud(net_nb, i, dvn_global_worker_data.pipecloud);
					}
				}
				break;
			}

			case DVNPACKET_GLOBAL_BCAST:
			case DVNPACKET_LOCAL_BCAST: {
				if(dest_layer & DVNPACKET_LAYER_OPT_DLOCAL) {
					/* would this ever happen? hopefully not. */
					for(int i=0; i <= dvn_global_worker_data.total_workers; i++)
						nbdf_send_pipecloud(net_nb, i, dvn_global_worker_data.pipecloud);
				} else {
					for(int i=0; i <= dvn_global_worker_data.total_workers; i++) {
						if(i == dvn_global_worker_data.process_id)
							continue;
						nbdf_send_pipecloud(net_nb, i, dvn_global_worker_data.pipecloud);
					}
				}
				break;
			}

			case DVNPACKET_LOCAL_SLAVE:
			case DVNPACKET_LOG:
			case DVNPACKET_MASTER:
			case DVNPACKET_SLAVE: {
				nbdf_send_pipecloud(net_nb, 0, dvn_global_worker_data.pipecloud);
				break;
			}

			case DVNPACKET_WORKER:{
				nbdf_send_pipecloud(net_nb, dest_major, dvn_global_worker_data.pipecloud);
				break;
			}
		}

	} else { /* non-worker context */
		switch(dest_type) {
			case DVNPACKET_WORKER_BCAST:{
				for(int i=1; i <= dvn_global_instance->slave->num_processes; i++)
					nbdf_send_pipecloud(net_nb, i, dvn_global_instance->slave->pipecloud);
				break;
			}

//			case DVNPACKET_SLAVE_BCAST: {
//				/* to remote slaves... */
//				for(int i=0; i<vector_size(dvn_global_instance->slave->slave_connections); i++) {
//					dvninstance_slave_connection_tp remote_slave_connection =
//							vector_get(dvn_global_instance->slave->slave_connections, i);
//
//					if(!remote_slave_connection->sock || remote_slave_connection->id < 0)
//						continue;
//
//					/* write to socket (always nonblocking) */
//					nbdf_send(net_nb, remote_slave_connection->sock);
//				}
//
//				/* local slave */
//				if(dest_layer & DVNPACKET_LAYER_OPT_DLOCAL)
//					dvn_slave_deposit(dvn_global_instance, net_nb);
//
//				break;
//			}

			case DVNPACKET_GLOBAL_BCAST: {
				/* broadcast to workers.... */
				for(int i=1; i <= dvn_global_instance->slave->num_processes; i++)
					nbdf_send_pipecloud(net_nb, i, dvn_global_instance->slave->pipecloud);

				/* then again to remote slaves */
				for(int i=0; i<vector_size(dvn_global_instance->slave->slave_connections); i++) {
					dvninstance_slave_connection_tp remote_slave_connection =
							vector_get(dvn_global_instance->slave->slave_connections, i);

					if(!remote_slave_connection->sock || remote_slave_connection->id < 0)
						continue;

					/* write to socket (always nonblocking) */
					nbdf_send(net_nb, remote_slave_connection->sock);
				}

				/* local slave */
				if(dest_layer & DVNPACKET_LAYER_OPT_DLOCAL)
					dvn_slave_deposit(dvn_global_instance, net_nb);

				break;
			}

			case DVNPACKET_LOCAL_SLAVE: {
				dvn_slave_deposit(dvn_global_instance, net_nb);
				break;
			}

			case DVNPACKET_LOCAL_BCAST: {
				/* broadcast to workers.... */
				for(int i=1; i <= dvn_global_instance->slave->num_processes; i++)
					nbdf_send_pipecloud(net_nb, i, dvn_global_instance->slave->pipecloud);

				/* local slave */
				if(dest_layer & DVNPACKET_LAYER_OPT_DLOCAL)
					dvn_slave_deposit(dvn_global_instance, net_nb);

				break;
			}

			case DVNPACKET_MASTER: {
				if(dvn_global_instance->my_instid == 0)
					dvn_slave_deposit(dvn_global_instance, net_nb);

				else {
                                        int key = 0;
					dvninstance_slave_connection_tp remote_slave_connection =
						g_hash_table_lookup(dvn_global_instance->slave->slave_connection_lookup, &key);

					if(remote_slave_connection && remote_slave_connection->sock)
						nbdf_send(net_nb, remote_slave_connection->sock);
				}
				break;
			}

			case DVNPACKET_SLAVE: {
				if(dvn_global_instance->my_instid == dest_major)
					dvn_slave_deposit(dvn_global_instance, net_nb);

				else {
					dvninstance_slave_connection_tp remote_slave_connection =
						g_hash_table_lookup(dvn_global_instance->slave->slave_connection_lookup, &dest_major);

					if(remote_slave_connection && remote_slave_connection->sock)
						nbdf_send(net_nb, remote_slave_connection->sock);
				}
				break;
			}

			case DVNPACKET_WORKER: {
				nbdf_send_pipecloud(net_nb, dest_major, dvn_global_instance->slave->pipecloud);
				break;
			}
		}
	}

	nbdf_free(net_nb);
}
