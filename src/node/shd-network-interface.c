/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "shadow.h"

enum NetworkInterfaceFlags {
	NIF_NONE = 0,
	NIF_SENDING = 1 << 0,
	NIF_RECEIVING = 1 << 1,
};

enum NetworkInterfaceQDisc {
	NIQ_NONE=0, NIQ_FIFO=1, NIQ_RR=2,
};

struct _NetworkInterface {
	enum NetworkInterfaceFlags flags;
	enum NetworkInterfaceQDisc qdisc;

	Network* network;
	Address* address;

	guint64 bwDownKiBps;
	gdouble timePerByteDown;
	guint64 bwUpKiBps;
	gdouble timePerByteUp;

	/* (protocol,port)-to-socket bindings */
	GHashTable* boundSockets;

	/* NIC input queue */
	GQueue* inBuffer;
	gsize inBufferSize;
	gsize inBufferLength;

	/* Transports wanting to send data out */
	GQueue* rrQueue;
	PriorityQueue* fifoQueue;

	/* PCAP flag, directory and file */
	gboolean logPcap;
	gchar* pcapDir;
	FILE *pcapFile;

	/* bandwidth accounting */
	SimulationTime lastTimeReceived;
	SimulationTime lastTimeSent;
	gdouble sendNanosecondsConsumed;
	gdouble receiveNanosecondsConsumed;
	MAGIC_DECLARE;
};

static void _networkinterface_pcapInit(NetworkInterface *interface) {
	if(!interface || !interface->logPcap || !interface->pcapFile) {
		return;
	}

	guint32 magic_number;   /* magic number */
	guint16 version_major;  /* major version number */
	guint16 version_minor;  /* minor version number */
	gint32  thiszone;       /* GMT to local correction */
	guint32 sigfigs;        /* accuracy of timestamps */
	guint32 snaplen;        /* max length of captured packets, in octets */
	guint32 network;        /* data link type */

	magic_number = 0xA1B2C3D4;
	version_major = 2;
	version_minor = 4;
	thiszone = 0;
	sigfigs = 0;
	snaplen = 65535;
	network = 1;

	fwrite(&magic_number, 1, sizeof(magic_number), interface->pcapFile);
	fwrite(&version_major, 1, sizeof(version_major), interface->pcapFile);
	fwrite(&version_minor, 1, sizeof(version_minor), interface->pcapFile);
	fwrite(&thiszone, 1, sizeof(thiszone), interface->pcapFile);
	fwrite(&sigfigs, 1, sizeof(sigfigs), interface->pcapFile);
	fwrite(&snaplen, 1, sizeof(snaplen), interface->pcapFile);
	fwrite(&network, 1, sizeof(network), interface->pcapFile);
}

static gint _networkinterface_compareSocket(const Socket* sa, const Socket* sb, gpointer userData) {
	Packet* pa = socket_peekNextPacket(sa);
	Packet* pb = socket_peekNextPacket(sa);
	return packet_getPriority(pa) > packet_getPriority(pb) ? +1 : -1;
}

NetworkInterface* networkinterface_new(Network* network, GQuark address, gchar* name,
		guint64 bwDownKiBps, guint64 bwUpKiBps, gboolean logPcap, gchar* pcapDir, gchar* qdisc,
		guint64 interfaceReceiveLength) {
	NetworkInterface* interface = g_new0(NetworkInterface, 1);
	MAGIC_INIT(interface);

	interface->network = network;
	interface->address = address_new(address, (const gchar*) name);

	/* interface speeds */
	interface->bwUpKiBps = bwUpKiBps;
	gdouble bytesPerSecond = (gdouble)(bwUpKiBps * 1024);
	interface->timePerByteUp = (gdouble) (((gdouble)SIMTIME_ONE_SECOND) / bytesPerSecond);
	interface->bwDownKiBps = bwDownKiBps;
	bytesPerSecond = (gdouble)(bwDownKiBps * 1024);
	interface->timePerByteDown = (gdouble) (((gdouble)SIMTIME_ONE_SECOND) / bytesPerSecond);

	/* incoming packet buffer */
	interface->inBuffer = g_queue_new();
	interface->inBufferSize = interfaceReceiveLength;

	/* incoming packets get passed along to sockets */
	interface->boundSockets = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, descriptor_unref);

	/* sockets tell us when they want to start sending */
	interface->rrQueue = g_queue_new();
	interface->fifoQueue = priorityqueue_new((GCompareDataFunc)_networkinterface_compareSocket, NULL, descriptor_unref);

	/* parse queuing discipline */
	if (qdisc && !g_ascii_strcasecmp(qdisc, "rr")) {
		interface->qdisc = NIQ_RR;
	} else {
		interface->qdisc = NIQ_FIFO;
	}

	/* log status */
	char addressStr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &address, addressStr, INET_ADDRSTRLEN);

	/* open the PCAP file for writing */
	interface->logPcap = logPcap;
	interface->pcapDir = pcapDir;
	interface->pcapFile = NULL;
	if(interface->logPcap) {
		GString *filename = g_string_new("");
		if (interface->pcapDir) {
			g_string_append(filename, interface->pcapDir);
			/* Append trailing slash if not present */
			if (!g_str_has_suffix(interface->pcapDir, "/")) {
				g_string_append(filename, "/");
			}
		} else {
			/* Use default directory */
			g_string_append(filename, "data/pcapdata/");
		}
		g_string_append_printf(filename, "%s-%s.pcap", name, addressStr);
		interface->pcapFile = fopen(filename->str, "w");
		if(!interface->pcapFile) {
			warning("error trying to open PCAP file '%s' for writing", filename->str);
		} else {
			_networkinterface_pcapInit(interface);
		}
	}

	info("bringing up network interface '%s' at '%s', %u KiB/s up and %u KiB/s down using queuing discipline %s",
			name, addressStr, bwUpKiBps, bwDownKiBps,
			interface->qdisc == NIQ_RR ? "rr" : "fifo");

	return interface;
}

void networkinterface_free(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* unref all packets sitting in our input buffer */
	while(interface->inBuffer && !g_queue_is_empty(interface->inBuffer)) {
		Packet* packet = g_queue_pop_head(interface->inBuffer);
		packet_unref(packet);
	}
	g_queue_free(interface->inBuffer);

	/* unref all sockets wanting to send */
	while(interface->rrQueue && !g_queue_is_empty(interface->rrQueue)) {
		Socket* socket = g_queue_pop_head(interface->rrQueue);
		descriptor_unref(socket);
	}
	g_queue_free(interface->rrQueue);

	priorityqueue_free(interface->fifoQueue);

	g_hash_table_destroy(interface->boundSockets);
	address_free(interface->address);

	if(interface->pcapFile) {
		fclose(interface->pcapFile);
	}

	MAGIC_CLEAR(interface);
	g_free(interface);
}

in_addr_t networkinterface_getIPAddress(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return address_toNetworkIP(interface->address);
}

gchar* networkinterface_getIPName(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return address_toHostIPString(interface->address);
}

guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return interface->bwUpKiBps;
}

guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return interface->bwDownKiBps;
}

gboolean networkinterface_isAssociated(NetworkInterface* interface, gint key) {
	MAGIC_ASSERT(interface);

	if(g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key))) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void networkinterface_associate(NetworkInterface* interface, Socket* socket) {
	MAGIC_ASSERT(interface);

	gint key = socket_getAssociationKey(socket);

	/* make sure there is no collision */
	g_assert(!networkinterface_isAssociated(interface, key));

	/* insert to our storage */
	g_hash_table_replace(interface->boundSockets, GINT_TO_POINTER(key), socket);
	descriptor_ref(socket);
}

void networkinterface_disassociate(NetworkInterface* interface, Socket* socket) {
	MAGIC_ASSERT(interface);

	gint key = socket_getAssociationKey(socket);

	/* we will no longer receive packets for this port, this unrefs descriptor */
	g_hash_table_remove(interface->boundSockets, GINT_TO_POINTER(key));
}

static void _networkinterface_pcapWritePacket(NetworkInterface *interface, Packet *packet) {
	if(!interface || !interface->logPcap || !interface->pcapFile || !packet) {
		return;
	}

	guint32 ts_sec;         /* timestamp seconds */
	guint32 ts_usec;        /* timestamp microseconds */
	guint32 incl_len;       /* number of octets of packet saved in file */
	guint32 orig_len;       /* actual length of packet */

	/* get the current time that the packet is being sent/received */
	SimulationTime now = worker_getPrivate()->clock_now;
	ts_sec = now / SIMTIME_ONE_SECOND;
	ts_usec = (now % SIMTIME_ONE_SECOND) / SIMTIME_ONE_MICROSECOND;

	/* get the header and payload lengths */
	guint headerSize = packet_getHeaderSize(packet);
	guint payloadLength = packet_getPayloadLength(packet);
	incl_len = headerSize + payloadLength;
	orig_len = headerSize + payloadLength;

	/* get the TCP header and the payload */
	PacketTCPHeader tcpHeader;
	guchar *payload = g_new0(guchar, payloadLength);
	packet_getTCPHeader(packet, &tcpHeader);
	packet_copyPayload(packet, 0, payload, payloadLength);

	/* write the PCAP packet header to the pcap file */
	fwrite(&ts_sec, sizeof(ts_sec), 1, interface->pcapFile);
	fwrite(&ts_usec, sizeof(ts_usec), 1, interface->pcapFile);
	fwrite(&incl_len, sizeof(incl_len), 1, interface->pcapFile);
	fwrite(&orig_len, sizeof(orig_len), 1, interface->pcapFile);

	/* write the ethernet header */
	guint8 destinationMAC[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
	guint8 sourceMAC[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
	guint16 type = htons(0x0800);

	fwrite(destinationMAC, 1, sizeof(destinationMAC), interface->pcapFile);
	fwrite(sourceMAC, 1, sizeof(sourceMAC), interface->pcapFile);
	fwrite(&type, 1, sizeof(type), interface->pcapFile);

	/* write the IP header */
	guint8 versionAndHeaderLength = 0x45;
	guint8 fields = 0x00;
	guint16 totalLength = htons(orig_len - 14);
	guint16 identification = 0x0000;
	guint16 flagsAndFragment = 0x0040;
	guint8 timeToLive = 64;
	guint8 protocol = 6;  /* TCP */
	guint16 headerChecksum = 0x0000;
	guint32 sourceIP = tcpHeader.sourceIP;
	guint32 destinationIP = tcpHeader.destinationIP;

	fwrite(&versionAndHeaderLength, 1, sizeof(versionAndHeaderLength), interface->pcapFile);
	fwrite(&fields, 1, sizeof(fields), interface->pcapFile);
	fwrite(&totalLength, 1, sizeof(totalLength), interface->pcapFile);
	fwrite(&identification, 1, sizeof(identification), interface->pcapFile);
	fwrite(&flagsAndFragment, 1, sizeof(flagsAndFragment), interface->pcapFile);
	fwrite(&timeToLive, 1, sizeof(timeToLive), interface->pcapFile);
	fwrite(&protocol, 1, sizeof(protocol), interface->pcapFile);
	fwrite(&headerChecksum, 1, sizeof(headerChecksum), interface->pcapFile);
	fwrite(&sourceIP, 1, sizeof(sourceIP), interface->pcapFile);
	fwrite(&destinationIP, 1, sizeof(destinationIP), interface->pcapFile);


	/* write the TCP header */
	guint16 sourcePort = tcpHeader.sourcePort;
	guint16 destinationPort = tcpHeader.destinationPort;
	guint32 sequence = tcpHeader.sequence;
	guint32 acknowledgement = 0;
	if(tcpHeader.flags & PTCP_ACK) {
		acknowledgement = htonl(tcpHeader.acknowledgement);
	}
	guint8 headerLength = 0x80;
	guint8 tcpFlags = 0;
	if(tcpHeader.flags & PTCP_RST) tcpFlags |= 0x04;
	if(tcpHeader.flags & PTCP_SYN) tcpFlags |= 0x02;
	if(tcpHeader.flags & PTCP_ACK) tcpFlags |= 0x10;
	if(tcpHeader.flags & PTCP_FIN) tcpFlags |= 0x01;
	guint16 window = tcpHeader.window;
	guint16 tcpChecksum = 0x0000;
	guint8 options[14] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	fwrite(&sourcePort, 1, sizeof(sourcePort), interface->pcapFile);
	fwrite(&destinationPort, 1, sizeof(destinationPort), interface->pcapFile);
	fwrite(&sequence, 1, sizeof(sequence), interface->pcapFile);
	fwrite(&acknowledgement, 1, sizeof(acknowledgement), interface->pcapFile);
	fwrite(&headerLength, 1, sizeof(headerLength), interface->pcapFile);
	fwrite(&tcpFlags, 1, sizeof(tcpFlags), interface->pcapFile);
	fwrite(&window, 1, sizeof(window), interface->pcapFile);
	fwrite(&tcpChecksum, 1, sizeof(tcpChecksum), interface->pcapFile);
	fwrite(options, 1, sizeof(options), interface->pcapFile);

	/* write payload data */
	if(payloadLength > 0) {
		fwrite(payload, 1, payloadLength, interface->pcapFile);
	}

	g_free(payload);
}

static void _networkinterface_dropInboundPacket(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/* drop incoming packet that traversed the network link from source */

	if(networkinterface_getIPAddress(interface) == packet_getSourceIP(packet)) {
		/* packet is on our own interface, so event destination is our node */
		PacketDroppedEvent* event = packetdropped_new(packet);
		worker_scheduleEvent((Event*)event, 1, 0);
	} else {
		/* let the network schedule the event with appropriate delays */
		network_scheduleRetransmit(interface->network, packet);
	}
}

static void _networkinterface_scheduleNextReceive(NetworkInterface* interface) {
	/* the next packets need to be received and processed */
	SimulationTime batchTime = worker_getConfig()->interfaceBatchTime;

	/* receive packets in batches */
	while(!g_queue_is_empty(interface->inBuffer) &&
			interface->receiveNanosecondsConsumed <= batchTime) {
		/* get the next packet */
		Packet* packet = g_queue_pop_head(interface->inBuffer);
		g_assert(packet);

		/* free up buffer space */
		guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
		interface->inBufferLength -= length;

		/* hand it off to the correct socket layer */
		gint key = packet_getDestinationAssociationKey(packet);
		Socket* socket = g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key));

		gchar* packetString = packet_getString(packet);
		debug("packet in: %s", packetString);
		g_free(packetString);

		_networkinterface_pcapWritePacket(interface, packet);

		/* if the socket closed, just drop the packet */
		if(socket) {
			gboolean needsRetransmit = socket_pushInPacket(socket, packet);
			if(needsRetransmit) {
				/* socket can not handle it now, so drop it */
				_networkinterface_dropInboundPacket(interface, packet);
			}
		}

		/* successfully received, calculate how long it took to 'receive' this packet */
		interface->receiveNanosecondsConsumed += (length * interface->timePerByteDown);
		tracker_addInputBytes(node_getTracker(worker_getPrivate()->cached_node),(guint64)length);
	}

	/*
	 * we need to call back and try to receive more, even if we didnt consume all
	 * of our batch time, because we might have more packets to receive then.
	 */
	SimulationTime receiveTime = (SimulationTime) floor(interface->receiveNanosecondsConsumed);
	if(receiveTime >= SIMTIME_ONE_NANOSECOND) {
		/* we are 'receiving' the packets */
		interface->flags |= NIF_RECEIVING;
		/* call back when the packets are 'received' */
		InterfaceReceivedEvent* event = interfacereceived_new(interface);
		/* event destination is our node */
		worker_scheduleEvent((Event*)event, receiveTime, 0);
	}
}

void networkinterface_packetArrived(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/* a packet arrived. lets try to receive or buffer it */
	guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
	gssize space = interface->inBufferSize - interface->inBufferLength;
	g_assert(space >= 0);

	if(length <= space) {
		/* we have space to buffer it */
		g_queue_push_tail(interface->inBuffer, packet);
		interface->inBufferLength += length;

		/* we need a trigger if we are not currently receiving */
		if(!(interface->flags & NIF_RECEIVING)) {
			_networkinterface_scheduleNextReceive(interface);
		}
	} else {
		/* buffers are full, drop packet */
		_networkinterface_dropInboundPacket(interface, packet);
	}
}

void networkinterface_received(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* we just finished receiving some packets */
	interface->flags &= ~NIF_RECEIVING;

	/* decide how much delay we get to absorb based on the passed time */
	SimulationTime now = worker_getPrivate()->clock_now;
	SimulationTime absorbInterval = now - interface->lastTimeReceived;

	if(absorbInterval > 0) {
		/* decide how much delay we get to absorb based on the passed time */
		gdouble newConsumed = interface->receiveNanosecondsConsumed - absorbInterval;
		interface->receiveNanosecondsConsumed = MAX(0, newConsumed);
	}

	interface->lastTimeReceived = now;

	/* now try to receive the next ones */
	_networkinterface_scheduleNextReceive(interface);
}

void networkinterface_packetDropped(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/*
	 * someone dropped a packet belonging to our interface
	 * hand it off to the correct socket layer
	 */
	gint key = packet_getSourceAssociationKey(packet);
	Socket* socket = g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key));

	/* just ignore if the socket closed in the meantime */
	if(socket) {
		socket_droppedPacket(socket, packet);
	} else {
		in_addr_t ip = packet_getSourceIP(packet);
		gchar* ipString = address_ipToNewString(ip);
		debug("interface dropping packet from %s:%u, no socket registered at %i",
				ipString, packet_getSourcePort(packet), key);
		g_free(ipString);
	}
}

/* round robin queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectRoundRobin(NetworkInterface* interface) {
	Packet* packet = NULL;

	while(!packet && !g_queue_is_empty(interface->rrQueue)) {
		/* do round robin to get the next packet from the next socket */
		Socket* socket = g_queue_pop_head(interface->rrQueue);
		packet = socket_pullOutPacket(socket);

		if(socket_peekNextPacket(socket)) {
			/* socket has more packets, and is still reffed from before */
			g_queue_push_tail(interface->rrQueue, socket);
		} else {
			/* socket has no more packets, unref it from the sendable queue */
			descriptor_unref((Descriptor*) socket);
		}
	}

	return packet;
}

/* first-in-first-out queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectFirstInFirstOut(NetworkInterface* interface) {
	/* use packet priority field to select based on application ordering.
	 * this is really a simplification of prioritizing on timestamps. */
	Packet* packet = NULL;

	while(!packet && !priorityqueue_isEmpty(interface->fifoQueue)) {
		/* do fifo to get the next packet from the next socket */
		Socket* socket = priorityqueue_pop(interface->fifoQueue);
		packet = socket_pullOutPacket(socket);

		if(socket_peekNextPacket(socket)) {
			/* socket has more packets, and is still reffed from before */
			priorityqueue_push(interface->fifoQueue, socket);
		} else {
			/* socket has no more packets, unref it from the sendable queue */
			descriptor_unref((Descriptor*) socket);
		}
	}

	return packet;
}

static void _networkinterface_scheduleNextSend(NetworkInterface* interface) {
	/* the next packet needs to be sent according to bandwidth limitations.
	 * we need to spend time sending it before sending the next. */
	SimulationTime batchTime = worker_getConfig()->interfaceBatchTime;

	/* loop until we find a socket that has something to send */
	while(interface->sendNanosecondsConsumed <= batchTime) {

		/* choose which packet to send next based on our queuing discipline */
		Packet* packet;
		switch(interface->qdisc) {
			case NIQ_RR: {
				packet = _networkinterface_selectRoundRobin(interface);
				break;
			}
			case NIQ_FIFO:
			default: {
				packet = _networkinterface_selectFirstInFirstOut(interface);
				break;
			}
		}
		if(!packet) {
			break;
		}

		/* now actually send the packet somewhere */
		if(networkinterface_getIPAddress(interface) == packet_getDestinationIP(packet)) {
			/* packet will arrive on our own interface */
			PacketArrivedEvent* event = packetarrived_new(packet);
			/* event destination is our node */
			worker_scheduleEvent((Event*)event, 1, 0);
		} else {
			/* let the network schedule with appropriate delays */
			network_schedulePacket(interface->network, packet);
		}

		gchar* packetString = packet_getString(packet);
		debug("packet out: %s", packetString);
		g_free(packetString);

		/* successfully sent, calculate how long it took to 'send' this packet */
		guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);

		interface->sendNanosecondsConsumed += (length * interface->timePerByteUp);
		tracker_addOutputBytes(node_getTracker(worker_getPrivate()->cached_node),(guint64)length);
		_networkinterface_pcapWritePacket(interface, packet);
	}

	/*
	 * we need to call back and try to send more, even if we didnt consume all
	 * of our batch time, because we might have more packets to send then.
	 */
	SimulationTime sendTime = (SimulationTime) floor(interface->sendNanosecondsConsumed);
	if(sendTime >= SIMTIME_ONE_NANOSECOND) {
		/* we are 'sending' the packets */
		interface->flags |= NIF_SENDING;
		/* call back when the packets are 'sent' */
		InterfaceSentEvent* event = interfacesent_new(interface);
		/* event destination is our node */
		worker_scheduleEvent((Event*)event, sendTime, 0);
	}
}

void networkinterface_wantsSend(NetworkInterface* interface, Socket* socket) {
	MAGIC_ASSERT(interface);

	/* track the new socket for sending if not already tracking */
	switch(interface->qdisc) {
		case NIQ_RR: {
			if(!g_queue_find(interface->rrQueue, socket)) {
				descriptor_ref(socket);
				g_queue_push_tail(interface->rrQueue, socket);
			}
			break;
		}
		case NIQ_FIFO:
		default: {
			if(!priorityqueue_find(interface->fifoQueue, socket)) {
				descriptor_ref(socket);
				priorityqueue_push(interface->fifoQueue, socket);
			}
			break;
		}
	}

	/* trigger a send if we are currently idle */
	if(!(interface->flags & NIF_SENDING)) {
		_networkinterface_scheduleNextSend(interface);
	}
}

void networkinterface_sent(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* we just finished sending some packets */
	interface->flags &= ~NIF_SENDING;

	/* decide how much delay we get to absorb based on the passed time */
	SimulationTime now = worker_getPrivate()->clock_now;
	SimulationTime absorbInterval = now - interface->lastTimeSent;

	if(absorbInterval > 0) {
		gdouble newConsumed = interface->sendNanosecondsConsumed - absorbInterval;
		interface->sendNanosecondsConsumed = MAX(0, newConsumed);
	}

	interface->lastTimeSent = now;

	/* now try to send the next ones */
	_networkinterface_scheduleNextSend(interface);
}
