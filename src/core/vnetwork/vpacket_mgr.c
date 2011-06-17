/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include <stdlib.h>
#include <stdint.h>
#include <strings.h>

#include "vpacket_mgr.h"
#include "vpacket.h"
#include "shmcabinet_mgr.h"
#include "log.h"
#include "sysconfig.h"

extern uint8_t vci_can_share_memory(in_addr_t);

static enum rwlock_mgr_type vpacket_mgr_get_rwlock_type(char* lock_type_config);

vpacket_mgr_tp vpacket_mgr_create() {
	vpacket_mgr_tp vp_mgr = malloc(sizeof(vpacket_mgr_t));

	vp_mgr->use_shmcabinet = sysconfig_get_int("vnetwork_use_shmcabinet");
	vp_mgr->lock_regular_packets = sysconfig_get_int("vpacketmgr_lock_regular_mem_packets");
	vp_mgr->smc_mgr_packets = NULL;
	vp_mgr->smc_mgr_payloads = NULL;

	if(vp_mgr->use_shmcabinet) {
		enum rwlock_mgr_type packet_cabinet_lock = vpacket_mgr_get_rwlock_type(sysconfig_get_string("vpacketmgr_packets_cabinet_lock_type"));
		enum rwlock_mgr_type packet_slot_lock = vpacket_mgr_get_rwlock_type(sysconfig_get_string("vpacketmgr_packets_slot_lock_type"));
		enum rwlock_mgr_type payload_cabinet_lock = vpacket_mgr_get_rwlock_type(sysconfig_get_string("vpacketmgr_payloads_cabinet_lock_type"));
		enum rwlock_mgr_type payload_slot_lock = vpacket_mgr_get_rwlock_type(sysconfig_get_string("vpacketmgr_payloads_slot_lock_type"));

		uint32_t num = (uint32_t) sysconfig_get_int("vpacketmgr_packets_per_shmcabinet");
		uint32_t threshold = (uint32_t) sysconfig_get_int("vpacketmgr_packets_threshold_shmcabinet");

		vp_mgr->smc_mgr_packets = shmcabinet_mgr_create(sizeof(vpacket_t), num, threshold, packet_cabinet_lock, packet_slot_lock);

		num = (uint32_t) sysconfig_get_int("vpacketmgr_payloads_per_shmcabinet");
		threshold = (uint32_t) sysconfig_get_int("vpacketmgr_payloads_threshold_shmcabinet");

		vp_mgr->smc_mgr_payloads = shmcabinet_mgr_create(VPACKET_MSS, num, threshold, payload_cabinet_lock, payload_slot_lock);
	}

	return vp_mgr;
}

static enum rwlock_mgr_type vpacket_mgr_get_rwlock_type(char* lock_type_config) {
	if(strcasecmp(lock_type_config, SYSCONFIG_LOCK_STR_PTHREAD) == 0) {
		return RWLOCK_MGR_TYPE_PTHREAD;
	} else if(strcasecmp(lock_type_config, SYSCONFIG_LOCK_STR_SEMAPHORE) == 0) {
		return RWLOCK_MGR_TYPE_SEMAPHORE;
	} else {
		return RWLOCK_MGR_TYPE_CUSTOM;
	}
}

void vpacket_mgr_destroy(vpacket_mgr_tp vp_mgr) {
	if(vp_mgr != NULL) {
		shmcabinet_mgr_destroy(vp_mgr->smc_mgr_packets);
		shmcabinet_mgr_destroy(vp_mgr->smc_mgr_payloads);
		free(vp_mgr);
	}
}

rc_vpacket_pod_tp vpacket_mgr_packet_create(vpacket_mgr_tp vp_mgr, uint8_t protocol,
		in_addr_t src_addr, in_port_t src_port, in_addr_t dst_addr, in_port_t dst_port,
		enum vpacket_tcp_flags flags, uint32_t seq_number, uint32_t ack_number, uint32_t advertised_window,
		uint16_t data_size, const void* data) {
	/* get vpod memory */
	vpacket_pod_tp vp_pod = malloc(sizeof(vpacket_pod_t));
	vp_pod->vp_mgr = vp_mgr;
	vp_pod->pod_flags = VP_OWNED;

	vp_pod->packet_lock = NULL;
	vp_pod->payload_lock = NULL;

	/* if vp_mgr->smc_mgr is NULL, we'll be using pipecloud instead of shmcabinet */
	if(vp_mgr != NULL &&
			vp_mgr->smc_mgr_packets != NULL && vp_mgr->smc_mgr_payloads != NULL &&
			vci_can_share_memory(dst_addr)) {
		vp_pod->pod_flags |= VP_SHARED;

		/* get shared memory for packet itself */
		vp_pod->shmitem_packet = shmcabinet_mgr_alloc(vp_mgr->smc_mgr_packets);

		/* check for error */
		if(vp_pod->shmitem_packet == NULL) {
			dlogf(LOG_ERR, "vpacket_mgr_packet_create: can't create packet, no shared memory\n");
			free(vp_pod);
			return NULL;
		}

		/* setup packet pointer so shared memory is transparent */
		vp_pod->vpacket = (vpacket_tp) vp_pod->shmitem_packet->payload;

		if(data_size > 0) {
			/* get shared memory for data */
			vp_pod->shmitem_payload = shmcabinet_mgr_alloc(vp_mgr->smc_mgr_payloads);

			/* check for error */
			if(vp_pod->shmitem_payload == NULL) {
				dlogf(LOG_ERR, "vpacket_mgr_packet_create: can't create packet payload, no shared memory\n");
				shmcabinet_mgr_free(vp_mgr->smc_mgr_packets, vp_pod->shmitem_packet);
				free(vp_pod);
				return NULL;
			}

			/* setup packet payload pointer so shared memory is transparent */
			vp_pod->vpacket->payload = vp_pod->shmitem_payload->payload;
		} else {
			vp_pod->shmitem_payload = NULL;
			vp_pod->vpacket->payload = NULL;
		}
	} else {
		/* "regular" memory */
		vp_pod->shmitem_packet = NULL;
		vp_pod->shmitem_payload = NULL;
		vp_pod->vpacket = malloc(sizeof(vpacket_t));

		if(data_size > 0) {
			vp_pod->vpacket->payload = malloc(data_size);
		} else {
			vp_pod->vpacket->payload = NULL;
		}

		/* check if we need locks */
		vpacket_mgr_setup_locks(vp_pod);
	}

	rc_vpacket_pod_tp rc_vpacket = rc_vpacket_pod_create(vp_pod, &vpacket_mgr_vpacket_pod_destructor_cb);

	/* dont bother locking, since no one else has access yet */
	if(rc_vpacket != NULL && rc_vpacket->pod != NULL) {
		/* set contents */
		vpacket_set(rc_vpacket->pod->vpacket, protocol, src_addr, src_port, dst_addr, dst_port,
				flags, seq_number, ack_number, advertised_window, data_size, data);
	}

	return rc_vpacket;
}

/* vp_mgr=NULL and/or dst_addr=0 forces normal (not shared) memory */
rc_vpacket_pod_tp vpacket_mgr_empty_packet_create() {
	vpacket_pod_tp vp_pod = malloc(sizeof(vpacket_pod_t));

	vp_pod->vp_mgr = NULL;
	vp_pod->pod_flags = VP_NONE;
	vp_pod->shmitem_packet = NULL;
	vp_pod->shmitem_payload = NULL;

	vp_pod->packet_lock = NULL;
	vp_pod->payload_lock = NULL;

	vp_pod->vpacket = malloc(sizeof(vpacket_t));
	vp_pod->vpacket->payload = NULL;

	return rc_vpacket_pod_create(vp_pod, &vpacket_mgr_vpacket_pod_destructor_cb);
}

void vpacket_mgr_setup_locks(vpacket_pod_tp vp_pod) {
	if(vp_pod != NULL && vp_pod->vp_mgr != NULL) {
		if(vp_pod->vp_mgr->lock_regular_packets) {
			enum rwlock_mgr_type packet_lock = vpacket_mgr_get_rwlock_type(sysconfig_get_string("vpacketmgr_packets_lock_type"));
			vp_pod->packet_lock = rwlock_mgr_create(packet_lock, 0);

			if(vp_pod->vpacket != NULL && vp_pod->vpacket->payload != NULL) {
				enum rwlock_mgr_type payload_lock = vpacket_mgr_get_rwlock_type(sysconfig_get_string("vpacketmgr_payloads_lock_type"));
				vp_pod->payload_lock = rwlock_mgr_create(payload_lock, 0);
			}
		}
	}
}

rc_vpacket_pod_tp vpacket_mgr_attach_shared_packet(vpacket_mgr_tp vp_mgr,
		shmcabinet_info_tp shminfo_packet, uint32_t slot_id_packet,
		shmcabinet_info_tp shminfo_payload, uint32_t slot_id_payload) {
	if(vp_mgr == NULL) {
		return NULL;
	}

	/* get vp_pod memory */
	vpacket_pod_tp vp_pod = malloc(sizeof(vpacket_pod_t));
	vp_pod->vp_mgr = vp_mgr;
	vp_pod->pod_flags = VP_SHARED;
	vp_pod->packet_lock = NULL;
	vp_pod->payload_lock = NULL;

	/* shm for the packet */
	vp_pod->shmitem_packet = shmcabinet_mgr_open(vp_mgr->smc_mgr_packets, shminfo_packet, slot_id_packet);

	/* check error */
	if(vp_pod->shmitem_packet == NULL) {
		dlogf(LOG_ERR, "vpacket_mgr_get_shared_packet: can't create packet, problem connecting to shared memory\n");
		free(vp_pod);
		return NULL;
	}

	/* setup packet pointer so shared memory is transparent */
	vp_pod->vpacket = (vpacket_tp) vp_pod->shmitem_packet->payload;

	/* shm for the payload, if there is a payload */
	if(shminfo_payload != NULL) {
		vp_pod->shmitem_payload = shmcabinet_mgr_open(vp_mgr->smc_mgr_payloads, shminfo_payload, slot_id_payload);

		/* check error */
		if(vp_pod->shmitem_payload == NULL) {
			dlogf(LOG_ERR, "vpacket_mgr_get_shared_packet: can't create packet payload, problem connecting to shared memory\n");
			shmcabinet_mgr_free(vp_mgr->smc_mgr_payloads, vp_pod->shmitem_payload);
			free(vp_pod);
		}

		/* setup packet payload pointer so shared memory is transparent */
		vp_pod->vpacket->payload = vp_pod->shmitem_payload->payload;
	} else {
		vp_pod->shmitem_payload = NULL;
		vp_pod->vpacket->payload = NULL;
	}

	return rc_vpacket_pod_create(vp_pod, &vpacket_mgr_vpacket_pod_destructor_cb);
}

void vpacket_mgr_vpacket_pod_destructor_cb(vpacket_pod_tp vp_pod) {
	if(vp_pod != NULL) {
		if((vp_pod->pod_flags & VP_SHARED) && vp_pod->vp_mgr != NULL) {
			if(vp_pod->shmitem_packet != NULL) {
				shmcabinet_mgr_free(vp_pod->vp_mgr->smc_mgr_packets, vp_pod->shmitem_packet);
			}
			if(vp_pod->shmitem_payload != NULL) {
				shmcabinet_mgr_free(vp_pod->vp_mgr->smc_mgr_payloads, vp_pod->shmitem_payload);
			}
		} else if(vp_pod->vpacket != NULL) {
			if(vp_pod->vpacket->payload != NULL) {
				free(vp_pod->vpacket->payload);
			}
			free(vp_pod->vpacket);
		}

		if(vp_pod->packet_lock != NULL) {
			rwlock_mgr_destroy(vp_pod->packet_lock);
		}
		if(vp_pod->payload_lock != NULL) {
			rwlock_mgr_destroy(vp_pod->payload_lock);
		}

		free(vp_pod);
	} else {
		dlogf(LOG_WARN, "vpacket_pod_rc_destructor_cb: unable to destroy NULL pod\n");
	}
}

vpacket_tp vpacket_mgr_lockcontrol(rc_vpacket_pod_tp rc_vp_pod, enum vpacket_lockcontrol command) {
	vpacket_pod_tp vp_pod = rc_vpacket_pod_get(rc_vp_pod);

	enum vpacket_lockcontrol operation = command & (LC_OP_READLOCK | LC_OP_READUNLOCK | LC_OP_WRITELOCK | LC_OP_WRITEUNLOCK);
	enum vpacket_lockcontrol target = command & (LC_TARGET_PACKET | LC_TARGET_PAYLOAD);

	/* TODO needs refactoring */
	if(vp_pod != NULL) {
		vpacket_mgr_tp vp_mgr = vp_pod->vp_mgr;
		if((vp_pod->pod_flags & VP_SHARED)) {
			switch (operation) {
				case LC_OP_READLOCK: {
					if((target & LC_TARGET_PACKET) && (target & LC_TARGET_PAYLOAD)) {
						/* we can only lock the payload if there actually is one */
						if(vp_pod->shmitem_payload != NULL && vp_pod->shmitem_packet != NULL) {
							if(shmcabinet_mgr_readlock(vp_pod->shmitem_payload)) {
								if(shmcabinet_mgr_readlock(vp_pod->shmitem_packet)) {
									/* success! */
									return vp_pod->vpacket;
								} else {
									shmcabinet_mgr_readunlock(vp_pod->shmitem_payload);
									dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm packet (with payload) error LC_OP_READLOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
								}
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm payload error LC_OP_READLOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						} else if(vp_pod->shmitem_packet != NULL) {
							if(shmcabinet_mgr_readlock(vp_pod->shmitem_packet)) {
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm packet (no payload) error LC_OP_READLOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						}
					} else if(target & LC_TARGET_PACKET) {
						if(vp_pod->shmitem_packet != NULL) {
							if(shmcabinet_mgr_readlock(vp_pod->shmitem_packet)){
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm packet error LC_OP_READLOCK LC_TARGET_PACKET\n");
							}
						}
					} else if(target & LC_TARGET_PAYLOAD) {
						if(vp_pod->shmitem_payload != NULL) {
							if(shmcabinet_mgr_readlock(vp_pod->shmitem_payload)){
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm payload error LC_OP_READLOCK LC_TARGET_PAYLOAD\n");
							}
						}
					}

					break;
				}

				case LC_OP_READUNLOCK: {
					if(target & LC_TARGET_PACKET) {
						shmcabinet_mgr_readunlock(vp_pod->shmitem_packet);
					}
					if(target & LC_TARGET_PAYLOAD) {
						shmcabinet_mgr_readunlock(vp_pod->shmitem_payload);
					}

					break;
				}

				case LC_OP_WRITELOCK: {
					if((target & LC_TARGET_PACKET) && (target & LC_TARGET_PAYLOAD)) {
						/* we can only lock the payload if there actually is one */
						if(vp_pod->shmitem_payload != NULL && vp_pod->shmitem_packet != NULL) {
							if(shmcabinet_mgr_writelock(vp_pod->shmitem_payload)) {
								if(shmcabinet_mgr_writelock(vp_pod->shmitem_packet)) {
									/* success! v*/
									return vp_pod->vpacket;
								} else {
									shmcabinet_mgr_writeunlock(vp_pod->shmitem_payload);
									dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm packet (with payload) error LC_OP_WRITELOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
								}
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm payload error LC_OP_WRITELOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						} else if (vp_pod->shmitem_packet != NULL) {
							if(shmcabinet_mgr_writelock(vp_pod->shmitem_packet)) {
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm packet (no payload) error LC_OP_WRITELOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						}
					} else if(target & LC_TARGET_PACKET) {
						if (vp_pod->shmitem_packet != NULL) {
							if(shmcabinet_mgr_writelock(vp_pod->shmitem_packet)){
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm packet error LC_OP_WRITELOCK LC_TARGET_PACKET\n");
							}
						}
					} else if(target & LC_TARGET_PAYLOAD) {
						if(vp_pod->shmitem_payload != NULL) {
							if(shmcabinet_mgr_writelock(vp_pod->shmitem_payload)){
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: shm payload error LC_OP_WRITELOCK LC_TARGET_PAYLOAD\n");
							}
						}
					}

					break;
				}

				case LC_OP_WRITEUNLOCK: {
					if(target & LC_TARGET_PACKET) {
						shmcabinet_mgr_writeunlock(vp_pod->shmitem_packet);
					}
					if(target & LC_TARGET_PAYLOAD) {
						shmcabinet_mgr_writeunlock(vp_pod->shmitem_payload);
					}

					break;
				}

				default: {
					dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: undefined command\n");
				}
			}
		} else if(vp_mgr != NULL && vp_mgr->lock_regular_packets && vp_pod->vpacket != NULL){ /* non-shared mem packets */
			switch (operation) {
				case LC_OP_READLOCK: {
					if((target & LC_TARGET_PACKET) && (target & LC_TARGET_PAYLOAD)) {
						/* we can only lock the payload if there actually is one */
						if(vp_pod->vpacket->payload != NULL) {
							if(rwlock_mgr_readlock(vp_pod->payload_lock) == RWLOCK_MGR_SUCCESS) {
								if(rwlock_mgr_readlock(vp_pod->packet_lock) == RWLOCK_MGR_SUCCESS) {
									/* success! */
									return vp_pod->vpacket;
								} else {
									rwlock_mgr_readunlock(vp_pod->payload_lock);
									dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: packet (with payload) error LC_OP_READLOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
								}
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: payload error LC_OP_READLOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						} else {
							if(rwlock_mgr_readlock(vp_pod->packet_lock) == RWLOCK_MGR_SUCCESS) {
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: packet (no payload) error LC_OP_READLOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						}
					} else if(target & LC_TARGET_PACKET) {
						if(rwlock_mgr_readlock(vp_pod->packet_lock) == RWLOCK_MGR_SUCCESS){
							return vp_pod->vpacket;
						} else {
							dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: packet error LC_OP_READLOCK LC_TARGET_PACKET\n");
						}
					} else if(target & LC_TARGET_PAYLOAD) {
						if(vp_pod->vpacket->payload != NULL) {
							if(rwlock_mgr_readlock(vp_pod->payload_lock) == RWLOCK_MGR_SUCCESS){
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: payload error LC_OP_READLOCK LC_TARGET_PAYLOAD\n");
							}
						}
					}

					break;
				}

				case LC_OP_READUNLOCK: {
					if(target & LC_TARGET_PACKET) {
						rwlock_mgr_readunlock(vp_pod->packet_lock);
					}
					if(target & LC_TARGET_PAYLOAD) {
						rwlock_mgr_readunlock(vp_pod->payload_lock);
					}

					break;
				}

				case LC_OP_WRITELOCK: {
					if((target & LC_TARGET_PACKET) && (target & LC_TARGET_PAYLOAD)) {
						/* we can only lock the payload if there actually is one */
						if(vp_pod->vpacket->payload != NULL){
							if(rwlock_mgr_writelock(vp_pod->payload_lock) == RWLOCK_MGR_SUCCESS) {
								if(rwlock_mgr_writelock(vp_pod->packet_lock) == RWLOCK_MGR_SUCCESS) {
									return vp_pod->vpacket;
								} else {
									rwlock_mgr_writeunlock(vp_pod->payload_lock);
									dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: packet (with payload) error LC_OP_WRITELOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
								}
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: payload error LC_OP_WRITELOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						} else {
							if(rwlock_mgr_writelock(vp_pod->packet_lock) == RWLOCK_MGR_SUCCESS) {
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: packet (no payload) error LC_OP_WRITELOCK LC_TARGET_PACKET LC_TARGET_PAYLOAD\n");
							}
						}
					} else if(target & LC_TARGET_PACKET) {
						if(rwlock_mgr_writelock(vp_pod->packet_lock) == RWLOCK_MGR_SUCCESS){
							return vp_pod->vpacket;
						} else {
							dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: packet error LC_OP_WRITELOCK LC_TARGET_PACKET\n");
						}
					} else if(target & LC_TARGET_PAYLOAD) {
						if(vp_pod->vpacket->payload != NULL) {
							if(rwlock_mgr_writelock(vp_pod->payload_lock) == RWLOCK_MGR_SUCCESS){
								return vp_pod->vpacket;
							} else {
								dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: payload error LC_OP_WRITELOCK LC_TARGET_PAYLOAD\n");
							}
						}
					}

					break;
				}

				case LC_OP_WRITEUNLOCK: {
					if(target & LC_TARGET_PACKET) {
						rwlock_mgr_writeunlock(vp_pod->packet_lock);
					}
					if(target & LC_TARGET_PAYLOAD) {
						rwlock_mgr_writeunlock(vp_pod->payload_lock);
					}

					break;
				}

				default: {
					dlogf(LOG_WARN, "vpacket_mgr_lockcontrol: undefined command\n");
				}
			}
		} else {
			/* no locking */
			return vp_pod->vpacket;
		}
	}
	return NULL;
}
