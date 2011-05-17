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

#ifndef _netconst_h
#define _netconst_h

/* DVN Controller frames */
#define DVN_CPREFIX 0x4f6b
#define DVN_CFRAME_CONFIG 1
#define DVN_CFRAME_GETCONFIG 2
#define DVN_CFRAME_SHUTDOWN 3
#define DVN_CFRAME_START 4
#define DVN_CFRAME_CONNECT 5

/* ---------------------------- ALL DVN frames -----------------------------*/

/* i've outlined all packet types with their respective destination(s). -prc means
 * destined for the process layer, -sim means destined for sim layer. e.g. worker-prc means
 * destined for workers at the process layer. worker-sim would be workers, but meant to
 * make it all the way to the simulation layer.
 */

/* dest: worker-prc */
#define DVN_FRAME_STARTSIM      10       /**< Notifies worker to instantiate simulation layer worker */
#define DVN_FRAME_DIE           11       /**< Notifies worker to die. */

/* dest: slave-prc  */
#define DVN_FRAME_BOOTSTRAP     40       /**< Sent from a master to a new (uninit'd) slave to tell it its instance ID */
#define DVN_FRAME_IDENTIFY      41       /**< Sent from an initialized slave to a newly connected host to identify itself */
#define DVN_FRAME_ENGAGEIP      42       /**< Sent from master to a newly connected host to notify it of another online host */
#define DVN_FRAME_LOG           43       /**< Contains a log message */
#define DVN_FRAME_TICKTOCK		44

#define SIM_FRAME_START         100
#define SIM_FRAME_END           101
#define SIM_FRAME_OP            102
#define SIM_FRAME_TRACK		    103
#define SIM_FRAME_DONE_WORKER   104
#define SIM_FRAME_ERROR         105
#define SIM_FRAME_STATE         106
#define SIM_FRAME_DONE_SLAVE    107

#define SIM_FRAME_VCI_PACKET_NOPAYLOAD				210
#define SIM_FRAME_VCI_PACKET_PAYLOAD				211
#define SIM_FRAME_VCI_PACKET_NOPAYLOAD_SHMCABINET	212
#define SIM_FRAME_VCI_PACKET_PAYLOAD_SHMCABINET		213
#define SIM_FRAME_VCI_RETRANSMIT					214
#define SIM_FRAME_VCI_CLOSE							215

/* destination type selection - HOW do we route the packet */
#define DVNPACKET_WORKER_BCAST 1         /**< broadcast only to this machine's workers (DLOCAL applies in worker context) */
#define DVNPACKET_GLOBAL_BCAST 2         /**< broadcast to this machine's workers and both local and remote slaves (DLOCAL applies) */
//#define DVNPACKET_SLAVE_BCAST  3         /**< broadcast only to slaves (not workers). DLOCAL applies */
#define DVNPACKET_LOCAL_BCAST  4         /* broadcast to this machine's workers and local slave (DLOCAL applies) */
#define DVNPACKET_SLAVE        5         /**< send to a specific slave (anywhere) */
#define DVNPACKET_WORKER       6         /**< send to a specific worker on this machine */
#define DVNPACKET_MASTER       7         /**< send to master */
#define DVNPACKET_LOG          8         /**< contains logging information - route wherever it needs to go! */
#define DVNPACKET_LOCAL_SLAVE  9
#define DVNPACKET_TICKTOCK	   10

/* layer destination - where does the packet go? */
#define DVNPACKET_LAYER_PRC    1         /**< outter process layer */
#define DVNPACKET_LAYER_SIM    2         /**< simulation layer */
//#define DVNPACKET_LAYER_LOG    3         /**< logging layer */

/* layer options bitwise ORed with the layer */
#define DVNPACKET_LAYER_OPT_DLOCAL   16  /**< include local delivery on broadcast */






/* DVN frames */
/*#define NETCTL_CMD_SHUTDOWN 0
#define NETCTL_CMD_START 1

#define NETCTL_CMD_STAT_REQ 2
#define NETCTL_CMD_STAT_RESP 3
#define NETCTL_CMD_CONNECT 4
#define NETCTL_CMD_CONFIG 5
#define NETCTL_CMD_GETCONFIG 6

#define NETCTL_PREFIX 0x4f6a
#define NETSIM_PREFIX 0x4f6b

#define NETSIM_CMD_SHUTDOWN 0
#define NETSIM_CMD_START 1
#define NETSIM_CMD_WORKERDELIV 2
#define NETSIM_CMD_MASTERDELIV 3
#define NETSIM_CMD_BOOTSTRAP 4
#define NETSIM_CMD_ENGAGEIP 5
#define NETSIM_CMD_IDENTIFY 6
#define NETSIM_CMD_NODELOG 7

#define NET_TYPE_VCIMSG 1 */


#endif
