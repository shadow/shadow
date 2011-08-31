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

#include <glib.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "reference_counter.h"
#include "vbuffer.h"
#include "vci.h"
#include "log.h"
#include "vepoll.h"

enum vbuffer_type {
	VB_SEND_RETRANSMIT, VB_SEND_VWRITE, VB_SEND_CONTROL,
	VB_RECEIVE_UNPROCESSED, VB_RECEIVE_VREAD
};

enum vbuffer_action {
	VB_ADD, VB_REMOVE, VB_GET
};

static vbuffer_rbuf_tp vbuffer_create_receive_buffer(guint64 max_size, guint8 tcp_mode);
static vbuffer_sbuf_tp vbuffer_create_send_buffer(guint64 max_size, guint8 tcp_mode);
static void vbuffer_destroy_receive_buffer(vbuffer_rbuf_tp rbuf);
static void vbuffer_destroy_send_buffer(vbuffer_sbuf_tp sbuf);
static guint8 vbuffer_add_receive_helper(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, enum vbuffer_type vbt);
static guint8 vbuffer_add_send_helper(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, enum vbuffer_type vbt, guint32 key);
static rc_vpacket_pod_tp vbuffer_remove_receive_helper(vbuffer_tp vb, enum vbuffer_type vbt, enum vbuffer_action vba, guint16** read_offset, guint32 next_sequence);
static rc_vpacket_pod_tp vbuffer_remove_send_helper(vbuffer_tp vb, enum vbuffer_type vbt, enum vbuffer_action vba, guint32 key);

vbuffer_tp vbuffer_create(guint8 type, guint64 max_recv_space, guint64 max_send_space, vepoll_tp vep) {
	vbuffer_tp vb = malloc(sizeof(vbuffer_t));

	vb->rbuf = vbuffer_create_receive_buffer(max_recv_space, type == SOCK_STREAM);
	vb->sbuf = vbuffer_create_send_buffer(max_send_space, type == SOCK_STREAM);
	vb->vep = vep;

	return vb;
}

void vbuffer_destroy(vbuffer_tp vb) {
	vbuffer_destroy_receive_buffer(vb->rbuf);
	vb->rbuf = NULL;

	vbuffer_destroy_send_buffer(vb->sbuf);
	vb->sbuf = NULL;

	free(vb);
}

static vbuffer_rbuf_tp vbuffer_create_receive_buffer(guint64 max_size, guint8 tcp_mode) {
	vbuffer_rbuf_tp rbuf = malloc(sizeof(vbuffer_rbuf_t));
	rbuf->max_size = max_size;
	rbuf->current_size = 0;
	rbuf->num_packets = 0;

	rbuf->vread = g_queue_new();
	rbuf->data_offset = 0;

	if(tcp_mode) {
		rbuf->tcp_unprocessed = orderedlist_create();
	} else {
		rbuf->tcp_unprocessed = NULL;
	}

	return rbuf;
}

static void vbuffer_destroy_receive_buffer(vbuffer_rbuf_tp rbuf) {
	if(rbuf != NULL) {
		if(rbuf->tcp_unprocessed != NULL) {
			while(orderedlist_length(rbuf->tcp_unprocessed) > 0) {
				rc_vpacket_pod_tp rc_packet = orderedlist_remove_last(rbuf->tcp_unprocessed);
				rc_vpacket_pod_release(rc_packet);
			}
			orderedlist_destroy(rbuf->tcp_unprocessed, 1);
			rbuf->tcp_unprocessed = NULL;
		}

		if(rbuf->vread != NULL) {
			while(g_queue_get_length(rbuf->vread) > 0) {
				rc_vpacket_pod_tp rc_packet = g_queue_pop_tail(rbuf->vread);
				rc_vpacket_pod_release(rc_packet);
			}
			g_queue_free(rbuf->vread);
			rbuf->vread = NULL;
		}

		rbuf->max_size = 0;
		rbuf->current_size = 0;
		rbuf->data_offset = 0;
		rbuf->num_packets = 0;
		free(rbuf);
	}
}

static vbuffer_sbuf_tp vbuffer_create_send_buffer(guint64 max_size, guint8 tcp_mode) {
	vbuffer_sbuf_tp sbuf = malloc(sizeof(vbuffer_sbuf_t));
	sbuf->max_size = max_size;
	sbuf->current_size = 0;
	sbuf->num_packets = 0;

	sbuf->vwrite = orderedlist_create();

	if(tcp_mode) {
		sbuf->tcp_retransmit = orderedlist_create();
		sbuf->tcp_control = g_queue_new();
	} else {
		sbuf->tcp_retransmit = NULL;
		sbuf->tcp_control = NULL;
	}
	return sbuf;
}

static void vbuffer_destroy_send_buffer(vbuffer_sbuf_tp sbuf) {
	if(sbuf != NULL) {
		if(sbuf->vwrite != NULL) {
			while(orderedlist_length(sbuf->vwrite) > 0) {
				rc_vpacket_pod_tp rc_packet = orderedlist_remove_last(sbuf->vwrite);
				rc_vpacket_pod_release(rc_packet);
			}

			orderedlist_destroy(sbuf->vwrite, 1);
			sbuf->vwrite = NULL;
		}

		if(sbuf->tcp_retransmit != NULL) {
			while(orderedlist_length(sbuf->tcp_retransmit) > 0) {
				rc_vpacket_pod_tp rc_packet = orderedlist_remove_last(sbuf->tcp_retransmit);
				rc_vpacket_pod_release(rc_packet);
			}
			orderedlist_destroy(sbuf->tcp_retransmit, 1);
			sbuf->tcp_retransmit = NULL;
		}

		if(sbuf->tcp_control != NULL) {
			while(g_queue_get_length(sbuf->tcp_control) > 0) {
				rc_vpacket_pod_tp rc_packet = g_queue_pop_head(sbuf->tcp_control);
				rc_vpacket_pod_release(rc_packet);
			}
			g_queue_free(sbuf->tcp_control);
			sbuf->tcp_control = NULL;
		}

		sbuf->max_size = 0;
		sbuf->current_size = 0;
		sbuf->num_packets = 0;
		free(sbuf);
	}
}

/* returns 1 if the packet was buffered, 0 otherwise */
static guint8 vbuffer_add_receive_helper(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, enum vbuffer_type vbt) {
	rc_vpacket_pod_retain_stack(rc_packet);
	guint8 result = 0;

	if(vb != NULL && vb->rbuf != NULL) {
		vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
		if(packet != NULL) {
			if(packet->data_size <= vbuffer_receive_space_available(vb)) {
				/* add the packet to the correct buffer */
				switch (vbt) {
					case VB_RECEIVE_UNPROCESSED:;
						/* UDP uses 0 as key because we do not care about order. */
						guint64 listkey = 0;
						if(packet->header.protocol == SOCK_STREAM) {
							listkey = packet->tcp_header.sequence_number;
						}
						orderedlist_add(vb->rbuf->tcp_unprocessed, listkey, rc_packet);
						break;

					case VB_RECEIVE_VREAD:
						g_queue_push_tail(vb->rbuf->vread, rc_packet);
						break;

					default:
						dlogf(LOG_CRIT, "vbuffer_add_receive_helper: must specify buffer\n");
						vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
						goto ret;
				}

				rc_vpacket_pod_retain(rc_packet);
				vb->rbuf->current_size += packet->data_size;


				vb->rbuf->num_packets++;

				/* success, packet was buffered */
				result = 1;
			} else {
				debugf("vbuffer_add_receive_helper: no space to store\n");
			}
			vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
		}
	}

ret:

	/* check current state of our user buffer and tell vepoll */
	if(vbuffer_is_readable(vb)) {
		vepoll_mark_available(vb->vep, VEPOLL_READ);
	} else {
		vepoll_mark_unavailable(vb->vep, VEPOLL_READ);
	}

	rc_vpacket_pod_release_stack(rc_packet);
	return result;
}

guint8 vbuffer_add_receive(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet) {
	return vbuffer_add_receive_helper(vb, rc_packet, VB_RECEIVE_UNPROCESSED);
}

guint8 vbuffer_add_read(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet){
	return vbuffer_add_receive_helper(vb, rc_packet, VB_RECEIVE_VREAD);
}

/* returns 1 if the packet was buffered, 0 otherwise */
static guint8 vbuffer_add_send_helper(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, enum vbuffer_type vbt, guint32 key) {
	rc_vpacket_pod_retain_stack(rc_packet);
	guint8 result = 0;

	if(vb != NULL && vb->sbuf != NULL && rc_packet != NULL) {
		vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
		if(packet != NULL) {
			if(packet->data_size <= vbuffer_send_space_available(vb)) {
				/* add the packet to the correct send buffer */
				switch (vbt) {
					case VB_SEND_RETRANSMIT:
						orderedlist_add(vb->sbuf->tcp_retransmit, key, rc_packet);
						break;

					case VB_SEND_VWRITE:
						orderedlist_add(vb->sbuf->vwrite, key, rc_packet);
						break;

					case VB_SEND_CONTROL:
						g_queue_push_tail(vb->sbuf->tcp_control, rc_packet);
						break;

					default:
						dlogf(LOG_CRIT, "vbuffer_add_send_helper: must specify buffer\n");
						vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
						goto ret;
				}

				rc_vpacket_pod_retain(rc_packet);
				vb->sbuf->current_size += packet->data_size;

				vb->sbuf->num_packets++;

				/* success, packet was buffered */
				result = 1;
			} else {
				debugf("vbuffer_add_send_helper: no space to store\n");
			}
			vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
		}
	}

ret:

	/* check current state of our user buffer and tell vepoll */
	if(vbuffer_is_writable(vb)) {
		vepoll_mark_available(vb->vep, VEPOLL_WRITE);
	} else {
		vepoll_mark_unavailable(vb->vep, VEPOLL_WRITE);
	}

	rc_vpacket_pod_release_stack(rc_packet);
	return result;
}

guint8 vbuffer_add_send(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, guint32 transmit_key) {
	return vbuffer_add_send_helper(vb, rc_packet, VB_SEND_VWRITE, transmit_key);
}

guint8 vbuffer_add_retransmit(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, guint32 retransmit_key) {
	return vbuffer_add_send_helper(vb, rc_packet, VB_SEND_RETRANSMIT, retransmit_key);
}

guint8 vbuffer_add_control(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet) {
	return vbuffer_add_send_helper(vb, rc_packet, VB_SEND_CONTROL, 0);
}

static rc_vpacket_pod_tp vbuffer_remove_receive_helper(vbuffer_tp vb, enum vbuffer_type vbt, enum vbuffer_action vba, guint16** read_offset, guint32 next_sequence) {
	rc_vpacket_pod_tp rc_packet = NULL;

	if(vb != NULL && vb->rbuf != NULL) {

		if(vba == VB_GET) {
			switch (vbt) {
				case VB_RECEIVE_UNPROCESSED:;
					/* check if the packet is in correct sequence */
					rc_vpacket_pod_tp rc_packet_peek = orderedlist_peek_first_value(vb->rbuf->tcp_unprocessed);

					vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet_peek, LC_OP_READLOCK | LC_TARGET_PACKET);
					if(packet != NULL) {
						if(packet->tcp_header.sequence_number == next_sequence) {
							rc_packet = rc_packet_peek;
						}
						vpacket_mgr_lockcontrol(rc_packet_peek, LC_OP_READUNLOCK | LC_TARGET_PACKET);
					}
					break;

				case VB_RECEIVE_VREAD:
					rc_packet = g_queue_peek_head(vb->rbuf->vread);
					*read_offset = &vb->rbuf->data_offset;
					break;

				default:
					dlogf(LOG_CRIT, "vbuffer_remove_receive_helper: must specify buffer\n");
					goto ret;
			}

			/* packet poginter is copied */
			rc_vpacket_pod_retain(rc_packet);
		} else if(vba == VB_REMOVE) {
			switch (vbt) {
				case VB_RECEIVE_UNPROCESSED:;
					/* check if the packet is in correct sequence */
					rc_vpacket_pod_tp rc_packet_peek = orderedlist_peek_first_value(vb->rbuf->tcp_unprocessed);
					vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet_peek, LC_OP_READLOCK | LC_TARGET_PACKET);
					if(packet != NULL) {
						if(packet->tcp_header.sequence_number == next_sequence) {
							rc_packet = orderedlist_remove_first(vb->rbuf->tcp_unprocessed);
						}
						vpacket_mgr_lockcontrol(rc_packet_peek, LC_OP_READUNLOCK | LC_TARGET_PACKET);
					}
					break;

				case VB_RECEIVE_VREAD:
					rc_packet = g_queue_pop_head(vb->rbuf->vread);
					vb->rbuf->data_offset = 0;
					break;

				default:
					dlogf(LOG_CRIT, "vbuffer_remove_receive_helper: must specify buffer\n");
					goto ret;
			}

			/* if packet poginter was removed, it will be returned */

			vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
			if(packet != NULL) {
				vb->rbuf->current_size -= packet->data_size;
				vb->rbuf->num_packets--;
				vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
			} else {
				/* no packets available or packet not in sequence */
			}
		}
	}

ret:

	/* check current state of our user buffer and tell vepoll */
	if(vbuffer_is_readable(vb)) {
		vepoll_mark_available(vb->vep, VEPOLL_READ);
	} else {
		vepoll_mark_unavailable(vb->vep, VEPOLL_READ);
	}

	return rc_packet;
}

rc_vpacket_pod_tp vbuffer_get_read(vbuffer_tp vb, guint16** read_offset) {
	return vbuffer_remove_receive_helper(vb, VB_RECEIVE_VREAD, VB_GET, read_offset, 0);
}

rc_vpacket_pod_tp vbuffer_remove_read(vbuffer_tp vb) {
	return vbuffer_remove_receive_helper(vb, VB_RECEIVE_VREAD, VB_REMOVE, NULL, 0);
}

rc_vpacket_pod_tp vbuffer_get_tcp_unprocessed(vbuffer_tp vb, guint32 next_sequence) {
	return vbuffer_remove_receive_helper(vb, VB_RECEIVE_UNPROCESSED, VB_GET, NULL, next_sequence);
}

rc_vpacket_pod_tp vbuffer_remove_tcp_unprocessed(vbuffer_tp vb, guint32 next_sequence) {
	return vbuffer_remove_receive_helper(vb, VB_RECEIVE_UNPROCESSED, VB_REMOVE, NULL, next_sequence);
}

static rc_vpacket_pod_tp vbuffer_remove_send_helper(vbuffer_tp vb, enum vbuffer_type vbt, enum vbuffer_action vba, guint32 key) {
	rc_vpacket_pod_tp rc_packet = NULL;

	if(vb != NULL && vb->sbuf != NULL) {

		if(vba == VB_GET) {
			switch (vbt) {
				case VB_SEND_RETRANSMIT:
					debugf("vbuffer_remove_send_helper: get retransmit unsupported\n");
					goto ret;

				case VB_SEND_VWRITE:
					if(orderedlist_peek_first_key(vb->sbuf->vwrite) <= key) {
						rc_packet = orderedlist_peek_first_value(vb->sbuf->vwrite);
						break;
					} else {
						goto ret;
					}

				case VB_SEND_CONTROL:
					debugf("vbuffer_remove_send_helper: get retransmit unsupported\n");
					goto ret;

				default:
					dlogf(LOG_CRIT, "vbuffer_remove_send_helper: must specify buffer\n");
					goto ret;
			}

			/* packet poginter is copied */
			rc_vpacket_pod_retain(rc_packet);
		} else if(vba == VB_REMOVE) {
			switch (vbt) {
				case VB_SEND_RETRANSMIT:
					rc_packet = orderedlist_remove(vb->sbuf->tcp_retransmit, key);
					break;

				case VB_SEND_VWRITE:
					if(orderedlist_peek_first_key(vb->sbuf->vwrite) <= key) {
						rc_packet = orderedlist_remove_first(vb->sbuf->vwrite);
						break;
					} else {
						goto ret;
					}

				case VB_SEND_CONTROL:
					rc_packet = g_queue_pop_head(vb->sbuf->tcp_control);
					break;

				default:
					dlogf(LOG_CRIT, "vbuffer_remove_send_helper: must specify buffer\n");
					goto ret;
			}

			/* if packet poginter was removed, it will be returned */

			vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
			if(packet != NULL) {
				vb->sbuf->current_size -= packet->data_size;
				vb->sbuf->num_packets--;
				vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
			} else {
				dlogf(LOG_WARN, "vbuffer_remove_send_helper: packet is NULL, key was %u\n", key);
			}
		}
	}

ret:

	/* check current state of our user buffer and tell vepoll */
	if(vbuffer_is_writable(vb)) {
		vepoll_mark_available(vb->vep, VEPOLL_WRITE);
	} else {
		vepoll_mark_unavailable(vb->vep, VEPOLL_WRITE);
	}

	return rc_packet;
}

rc_vpacket_pod_tp vbuffer_get_send(vbuffer_tp vb) {
	return vbuffer_remove_send_helper(vb, VB_SEND_VWRITE, VB_GET, 0);
}

rc_vpacket_pod_tp vbuffer_remove_send(vbuffer_tp vb, guint32 transmit_key) {
	return vbuffer_remove_send_helper(vb, VB_SEND_VWRITE, VB_REMOVE, transmit_key);
}

rc_vpacket_pod_tp vbuffer_remove_tcp_retransmit(vbuffer_tp vb, guint32 retransmit_key) {
	return vbuffer_remove_send_helper(vb, VB_SEND_RETRANSMIT, VB_REMOVE, retransmit_key);
}

rc_vpacket_pod_tp vbuffer_remove_tcp_control(vbuffer_tp vb) {
	return vbuffer_remove_send_helper(vb, VB_SEND_CONTROL, VB_REMOVE, 0);
}

guint8 vbuffer_is_empty(vbuffer_tp vb) {
	if(vb != NULL) {
		/* we return true only if none of the buffers have data or packets
		 * to handle. since packets without user data don't take up any "space"
		 * we must also check the list sizes */

		if(vb->sbuf != NULL &&
				((vb->sbuf->current_size > 0) || (vb->sbuf->num_packets > 0))) {
			return 0;
		}

		if(vb->rbuf != NULL &&
				((vb->rbuf->current_size > 0) || (vb->rbuf->num_packets > 0))) {
			return 0;
		}
	}

	/* if we got here we are empty */
	return 1;
}

size_t vbuffer_send_space_available(vbuffer_tp vb) {
	if(vb != NULL && vb->sbuf != NULL) {
		return vb->sbuf->max_size - vb->sbuf->current_size;
	}
	return 0;
}

size_t vbuffer_receive_space_available(vbuffer_tp vb) {
	if(vb != NULL && vb->rbuf != NULL) {
		size_t s = (size_t) vb->rbuf->max_size - vb->rbuf->current_size;
		return s;
	}
	return 0;
}

guint8 vbuffer_is_readable(vbuffer_tp vb) {
	if(vb != NULL && vb->rbuf != NULL && vb->rbuf->vread != NULL) {
		return g_queue_get_length(vb->rbuf->vread) > 0;
	}
	return 0;
}

guint8 vbuffer_is_writable(vbuffer_tp vb) {
	if(vb != NULL && vb->sbuf != NULL && vb->sbuf->vwrite != NULL) {
		return vbuffer_send_space_available(vb) > 0;
	}
	return 0;
}

void vbuffer_set_size(vbuffer_tp vb, guint64 rbuf_max, guint64 sbuf_max) {
	if (vb != NULL) {
		/* todo do we need minimums */
		if(vb->sbuf != NULL) {
			vb->sbuf->max_size = sbuf_max;
		}
		if(vb->rbuf != NULL) {
			vb->rbuf->max_size = rbuf_max;
		}
	}
}

void vbuffer_clear_send(vbuffer_tp vb) {
	if(vb != NULL && vb->sbuf != NULL) {
		while(orderedlist_length(vb->sbuf->vwrite) > 0) {
			rc_vpacket_pod_tp rc_packet = vbuffer_remove_send(vb, UINT32_MAX);
			rc_vpacket_pod_release(rc_packet);
		}
	}
}

void vbuffer_clear_tcp_retransmit(vbuffer_tp vb, guint8 only_clear_acked, guint32 acknum) {
	if(!only_clear_acked) {
		acknum = UINT32_MAX;
	}
	/* last_byte_in_packet <= acknum */
	while(orderedlist_peek_first_key(vb->sbuf->tcp_retransmit) < acknum) {
		rc_vpacket_pod_tp rc_packet = orderedlist_remove_first(vb->sbuf->tcp_retransmit);

		vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
		if(packet != NULL) {
			vb->sbuf->current_size -= packet->data_size;
			vb->sbuf->num_packets--;
		}
		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

		rc_vpacket_pod_release(rc_packet);
	}
}

gint vbuffer_get_send_length(vbuffer_tp vb) {
	if(vb != NULL && vb->sbuf != NULL) {
		return orderedlist_length(vb->sbuf->vwrite);
	} else {
		return 0;
	}
}

guint8 vbuffer_is_empty_send_control(vbuffer_tp vb) {
	if(vb->sbuf->tcp_control != NULL) {
		return g_queue_get_length(vb->sbuf->tcp_control) == 0;
	}
	return 0;
}
