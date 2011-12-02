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

#include "shadow.h"

void transport_free(Transport* transport) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);

	if(transport->boundString) {
		g_free(transport->boundString);
	}

	MAGIC_CLEAR(transport);
	transport->vtable->free((Descriptor*)transport);
}

void transport_close(Transport* transport) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	transport->vtable->close((Descriptor*)transport);
}

DescriptorFunctionTable transport_functions = {
	(DescriptorFunc) transport_close,
	(DescriptorFunc) transport_free,
	MAGIC_VALUE
};

void transport_init(Transport* transport, TransportFunctionTable* vtable, enum DescriptorType type, gint handle) {
	g_assert(transport && vtable);

	descriptor_init(&(transport->super), type, &transport_functions, handle);

	MAGIC_INIT(transport);
	MAGIC_INIT(vtable);

	transport->vtable = vtable;
	transport->protocol = type == DT_TCPSOCKET ? PTCP : type == DT_UDPSOCKET ? PUDP : PLOCAL;
	transport->inputBuffer = g_queue_new();
	transport->inputBufferSize = CONFIG_RECV_BUFFER_SIZE;
	transport->outputBuffer = g_queue_new();
	transport->outputBufferSize = CONFIG_SEND_BUFFER_SIZE;
}

in_addr_t transport_getBinding(Transport* transport) {
	MAGIC_ASSERT(transport);
	if(transport->flags & TF_BOUND) {
		return transport->boundAddress;
	} else {
		return 0;
	}
}

void transport_setBinding(Transport* transport, in_addr_t boundAddress, in_port_t port) {
	MAGIC_ASSERT(transport);
	transport->boundAddress = boundAddress;
	transport->boundPort = port;

	/* store the new ascii name of this transport endpoint */
	if(transport->boundString) {
		g_free(transport->boundString);
	}
	GString* stringBuffer = g_string_new(NTOA(boundAddress));
	g_string_append_printf(stringBuffer, ":%u (descriptor %i)", ntohs(port), transport->super.handle);
	transport->boundString = g_string_free(stringBuffer, FALSE);

	transport->associationKey = PROTOCOL_DEMUX_KEY(transport->protocol, port);
	transport->flags |= TF_BOUND;
}

gint transport_getAssociationKey(Transport* transport) {
	MAGIC_ASSERT(transport);
	g_assert(transport_getBinding(transport));
	return transport->associationKey;
}

gboolean transport_pushInPacket(Transport* transport, Packet* packet) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	return transport->vtable->process(transport, packet);
}

Packet* transport_pullOutPacket(Transport* transport) {
	return transport_removeFromOutputBuffer(transport);
}

gssize transport_sendUserData(Transport* transport, gconstpointer buffer, gsize nBytes,
		in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	return transport->vtable->send(transport, buffer, nBytes, ip, port);
}

gssize transport_receiveUserData(Transport* transport, gpointer buffer, gsize nBytes,
		in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	return transport->vtable->receive(transport, buffer, nBytes, ip, port);
}

void transport_droppedPacket(Transport* transport, Packet* packet) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	transport->vtable->dropped(transport, packet);
}

gsize transport_getInputBufferSpace(Transport* transport) {
	MAGIC_ASSERT(transport);
	g_assert(transport->inputBufferSize >= transport->inputBufferLength);
	return (transport->inputBufferSize - transport->inputBufferLength);
}

gboolean transport_addToInputBuffer(Transport* transport, Packet* packet) {
	MAGIC_ASSERT(transport);

	/* check if the packet fits */
	guint length = packet_getPayloadLength(packet);
	if(length > transport_getInputBufferSpace(transport)) {
		return FALSE;
	}

	/* add to our queue */
	packet_ref(packet);
	g_queue_push_tail(transport->inputBuffer, packet);
	transport->inputBufferLength += length;

	/* we just added a packet, so we are readable */
	if(transport->inputBufferLength > 0) {
		descriptor_adjustStatus((Descriptor*)transport, DS_READABLE, TRUE);
	}

	return TRUE;
}

Packet* transport_removeFromInputBuffer(Transport* transport) {
	MAGIC_ASSERT(transport);

	/* see if we have any packets */
	Packet* packet = g_queue_pop_head(transport->inputBuffer);
	if(packet) {
		/* just removed a packet */
		guint length = packet_getPayloadLength(packet);
		transport->inputBufferLength -= length;

		/* we are not readable if we are now empty */
		if(transport->inputBufferLength <= 0) {
			descriptor_adjustStatus((Descriptor*)transport, DS_READABLE, FALSE);
		}
	}

	return packet;
}

gsize transport_getOutputBufferSpace(Transport* transport) {
	MAGIC_ASSERT(transport);
	g_assert(transport->outputBufferSize >= transport->outputBufferLength);
	return (transport->outputBufferSize - transport->outputBufferLength);
}

gboolean transport_addToOutputBuffer(Transport* transport, Packet* packet) {
	MAGIC_ASSERT(transport);

	/* check if the packet fits */
	guint length = packet_getPayloadLength(packet);
	if(length > transport_getOutputBufferSpace(transport)) {
		return FALSE;
	}

	/* add to our queue */
	g_queue_push_tail(transport->outputBuffer, packet);
	transport->outputBufferLength += length;

	/* we just added a packet, we are no longer writable if full */
	if(transport_getOutputBufferSpace(transport) <= 0) {
		descriptor_adjustStatus((Descriptor*)transport, DS_WRITABLE, FALSE);
	}

	/* tell the interface to include us when sending out to the network */
	NetworkInterface* interface = node_lookupInterface(worker_getPrivate()->cached_node, transport->boundAddress);
	networkinterface_wantsSend(interface, transport);

	return TRUE;
}

Packet* transport_removeFromOutputBuffer(Transport* transport) {
	MAGIC_ASSERT(transport);

	/* see if we have any packets */
	Packet* packet = g_queue_pop_head(transport->outputBuffer);
	if(packet) {
		/* just removed a packet */
		guint length = packet_getPayloadLength(packet);
		transport->outputBufferLength -= length;

		/* we are writable if we now have space */
		if(transport_getOutputBufferSpace(transport) > 0) {
			descriptor_adjustStatus((Descriptor*)transport, DS_WRITABLE, TRUE);
		}
	}

	return packet;
}
