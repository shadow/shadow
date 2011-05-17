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
#include "sim.h"
#include "routing.h"
#include "netconst.h"

sim_slave_tp sim_slave_create (unsigned int my_id, unsigned int num_workers) {
	sim_slave_tp slave;

	slave=malloc(sizeof(*slave));
	if(!slave)
		printfault(EXIT_NOMEM, "sim_slave_create: Out of memory");

	slave->my_id = my_id;
	slave->num_workers = num_workers;
	slave->worker_turn = 0;
	slave->num_workers_complete = 0;

	return slave;
}

void sim_slave_destroy(sim_slave_tp sslave) {
	free(sslave);
	// TODO: sim slave needs better cleanup
	return;
}

void sim_slave_deposit(sim_slave_tp sslave, int frametype, nbdf_tp frame) {

	switch(frametype) {
		case SIM_FRAME_OP: {
			simop_tp sop = simop_nbdf_decode(frame);
			if(sop) {
				/* we need to manage CNODES commands so each process gets a fair share. all
				 * other ops get passthrough treatment to the workers */
				if(sop->type == OP_CREATE_NODES) {
					/* workers ids start at 1 */
					if(sslave->worker_turn < 1 || sslave->worker_turn > sslave->num_workers) {
						sslave->worker_turn = 1;
					}

					/* currently only one node created at a time... */
					dvn_packet_route(DVNPACKET_WORKER, DVNPACKET_LAYER_SIM,	sslave->worker_turn, SIM_FRAME_OP, frame);
					
					/* setup next guys turn */
					sslave->worker_turn++;
				} else {
					dvn_packet_route(DVNPACKET_WORKER_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_OP, frame);
				}

				free(sop->operation);
				free(sop);
			}
			break;
		}

		case SIM_FRAME_DONE_WORKER:
			sslave->num_workers_complete++;

			if(sslave->num_workers_complete == sslave->num_workers) {
				/* notify the master this slave is done.. */

				debugf("SSlave: All workers reported complete. Notifying master.\n");

				nbdf_tp done_frame = nbdf_construct("i", sslave->my_id);
				dvn_packet_route(DVNPACKET_MASTER, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_DONE_SLAVE, done_frame);
				nbdf_free(done_frame);
			}
			break;

		case SIM_FRAME_VCI_PACKET_NOPAYLOAD:
		case SIM_FRAME_VCI_PACKET_PAYLOAD:
		case SIM_FRAME_VCI_PACKET_NOPAYLOAD_SHMCABINET:
		case SIM_FRAME_VCI_PACKET_PAYLOAD_SHMCABINET:
		case SIM_FRAME_VCI_RETRANSMIT:
		case SIM_FRAME_VCI_CLOSE:
			/* VCI deliveries need to be routed to the proper. get the destination address out... blahblahblahblah  */
			// TODO: this should route, not broadcast. duh.

			//dvn_write_sim_worker_bcast(SIM_FRAME_VCIDELIV, sim_frame);
			debugf("SSlave: ****** VCI ROUTING HERE\n");
			break;

		case SIM_FRAME_TRACK:
			debugf("SSlave: ****** NODE TRACKING PACKET HERE\n");
			break;

		default:
			/* by default, we just remit packets to the worker processes */
			dvn_packet_route(DVNPACKET_WORKER_BCAST, DVNPACKET_LAYER_SIM, 0, frametype, frame);
			break;
	}
}
