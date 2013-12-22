/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

enum TCPState {
	TCPS_CLOSED, TCPS_LISTEN,
	TCPS_SYNSENT, TCPS_SYNRECEIVED, TCPS_ESTABLISHED,
	TCPS_FINWAIT1, TCPS_FINWAIT2, TCPS_CLOSING, TCPS_TIMEWAIT,
	TCPS_CLOSEWAIT, TCPS_LASTACK,
};

static const gchar* tcpStateStrings[] = {
	"TCPS_CLOSED", "TCPS_LISTEN",
	"TCPS_SYNSENT", "TCPS_SYNRECEIVED", "TCPS_ESTABLISHED",
	"TCPS_FINWAIT1", "TCPS_FINWAIT2", "TCPS_CLOSING", "TCPS_TIMEWAIT",
	"TCPS_CLOSEWAIT", "TCPS_LASTACK"
};

static const gchar* tcp_stateToAscii(enum TCPState state) {
	return tcpStateStrings[state];
}

enum TCPFlags {
	TCPF_NONE = 0,
	TCPF_LOCAL_CLOSED = 1 << 0,
	TCPF_REMOTE_CLOSED = 1 << 1,
	TCPF_EOF_SIGNALED = 1 << 2,
	TCPF_RESET_SIGNALED = 1 << 3,
	TCPF_WAS_ESTABLISHED = 1 << 4,
};

enum TCPError {
	TCPE_NONE = 0,
	TCPE_CONNECTION_RESET = 1 << 0,
	TCPE_SEND_EOF = 1 << 1,
	TCPE_RECEIVE_EOF = 1 << 2,
};

enum TCPChildState {
	TCPCS_NONE, TCPCS_INCOMPLETE, TCPCS_PENDING, TCPCS_ACCEPTED
};

typedef struct _TCPChild TCPChild;
struct _TCPChild {
	enum TCPChildState state;
	TCP* tcp;
	guint key; /* hash(peerIP, peerPort) */
	TCP* parent;
	MAGIC_DECLARE;
};

typedef struct _TCPServer TCPServer;
struct _TCPServer {
	/* all children of this server */
	GHashTable* children;
	/* pending children to accept in order. */
	GQueue *pending;
	/* maximum number of pending connections (capped at SOMAXCONN = 128) */
	gint pendingMaxLength;
	/* IP and port of the last peer trying to connect to us */
	in_addr_t lastPeerIP;
	in_port_t lastPeerPort;
	/* last interface IP we received on */
	in_addr_t lastIP;
	MAGIC_DECLARE;
};

struct _TCP {
	Socket super;

	enum TCPState state;
	enum TCPState stateLast;
	enum TCPFlags flags;
	enum TCPError error;

	/* sequence numbers we track for incoming packets */
	struct {
		/* initial receive sequence number */
		guint32 start;
		/* next packet we expect to receive */
		guint32 next;
		/* how far past next can we receive */
		guint32 window;
		/* used to make sure we get all data when other end closes */
		guint32 end;
	} receive;

	/* sequence numbers we track for outgoing packets */
	struct {
		/* packets we've sent but have yet to be acknowledged */
		guint32 unacked;
		/* next packet we can send */
		guint32 next;
		/* how far past next can we send */
		guint32 window;
		/* the last byte that was sent by the app, possibly not yet sent to the network */
		guint32 end;
		/* the last ack number we sent them */
		guint32 lastAcknowledgement;
		/* the last advertised window we sent them */
		guint32 lastWindow;
	} send;

	/* congestion control, sequence numbers used for AIMD and slow start */
	gboolean isSlowStart;
	struct {
		/* our current calculated congestion window */
		guint32 window;
		guint32 threshold;
		/* their last advertised window */
		guint32 lastWindow;
		/* send sequence number used for last window update */
		guint32 lastSequence;
		/* send ack number used from last window update */
		guint32 lastAcknowledgement;
	} congestion;

	/* TODO: these should probably be stamped when the network interface sends
	 * instead of when the tcp layer sends down to the socket layer */
	struct {
		SimulationTime lastDataSent;
		SimulationTime lastAckSent;
		SimulationTime lastDataReceived;
		SimulationTime lastAckReceived;
		gsize retransmitCount;
		guint32 rtt;
	} info;

	/* TCP throttles outgoing data packets if too many are in flight */
	GQueue* throttledOutput;
	gsize throttledOutputLength;

	/* TCP ensures that the user receives data in-order */
	GQueue* unorderedInput;
	gsize unorderedInputLength;

	/* keep track of the sequence numbers and lengths of packets we may need to
	 * retransmit in the future if they get dropped. this only holds information
	 * about packets with data, i.e. with a positive length. this is done
	 * so we can correctly track buffer length when data is acked.
	 */
	GHashTable* retransmission;
	gsize retransmissionLength;

	/* tracks a packet that has currently been only partially read, if any */
	Packet* partialUserDataPacket;
	guint partialOffset;

	/* if I am a server, I parent many multiplexed child sockets */
	TCPServer* server;

	/* if I am a multiplexed child, I have a pointer to my parent */
	TCPChild* child;

	MAGIC_DECLARE;
};

static TCPChild* _tcpchild_new(TCP* tcp, TCP* parent, in_addr_t peerIP, in_port_t peerPort) {
	MAGIC_ASSERT(tcp);
	MAGIC_ASSERT(parent);

	TCPChild* child = g_new0(TCPChild, 1);
	MAGIC_INIT(child);

	/* my parent can find me by my key */
	child->key = utility_ipPortHash(peerIP, peerPort);

	descriptor_ref(tcp);
	child->tcp = tcp;
	descriptor_ref(parent);
	child->parent = parent;

	child->state = TCPCS_INCOMPLETE;
	socket_setPeerName(&(child->tcp->super), peerIP, peerPort);

	/* the child is bound to the parent server's address, because all packets
	 * coming from the child should appear to be coming from the server itself */
	in_addr_t parentAddress;
	in_port_t parentPort;
	socket_getSocketName(&(parent->super), &parentAddress, &parentPort);
	socket_setSocketName(&(child->tcp->super), parentAddress, parentPort, TRUE);

	return child;
}

static void _tcpchild_free(TCPChild* child) {
	MAGIC_ASSERT(child);

	/* make sure our tcp doesnt try to free the child again */
	child->tcp->child = NULL;
	descriptor_unref(child->tcp);
	descriptor_unref(child->parent);

	MAGIC_CLEAR(child);
	g_free(child);
}

static TCPServer* _tcpserver_new(gint backlog) {
	TCPServer* server = g_new0(TCPServer, 1);
	MAGIC_INIT(server);

	server->children = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify) _tcpchild_free);
	server->pending = g_queue_new();
	server->pendingMaxLength = backlog;

	return server;
}

static void _tcpserver_free(TCPServer* server) {
	MAGIC_ASSERT(server);

	/* no need to destroy children in this queue */
	g_queue_free(server->pending);
	/* this will unref all children */
	g_hash_table_destroy(server->children);

	MAGIC_CLEAR(server);
	g_free(server);
}

static in_addr_t tcp_getIP(TCP* tcp) {
	in_addr_t ip = 0;
	if(tcp->server) {
		if(socket_isBound(&(tcp->super))) {
			socket_getSocketName(&(tcp->super), &ip, NULL);
		} else {
			ip = tcp->server->lastIP;
		}
	} else if(tcp->child) {
		if(socket_isBound(&(tcp->child->parent->super))) {
			socket_getSocketName(&(tcp->child->parent->super), &ip, NULL);
		} else {
			ip = tcp->child->parent->server->lastIP;
		}
	} else {
		socket_getSocketName(&(tcp->super), &ip, NULL);
	}
	return ip;
}

static in_addr_t tcp_getPeerIP(TCP* tcp) {
	in_addr_t ip = tcp->super.peerIP;
	if(tcp->server && ip == 0) {
		ip = tcp->server->lastPeerIP;
	}
	return ip;
}

static void _tcp_autotune(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	if(!CONFIG_TCPAUTOTUNE) {
		return;
	}

	/* our buffers need to be large enough to send and receive
	 * a full delay*bandwidth worth of bytes to keep the pipe full.
	 * but not too large that we'll just buffer everything. autotuning
	 * is meant to tune it to an optimal rate.
	 */

	in_addr_t sourceIP = tcp_getIP(tcp);
	in_addr_t destinationIP = tcp_getPeerIP(tcp);

	if(sourceIP == htonl(INADDR_ANY)) {
		/* source interface depends on destination */
		if(destinationIP == htonl(INADDR_LOOPBACK)) {
			sourceIP = htonl(INADDR_LOOPBACK);
		} else {
			sourceIP = host_getDefaultIP(worker_getCurrentHost());
		}
	}

	if(sourceIP == destinationIP) {
		/* 16 MiB as max */
		gsize inSize = socket_getInputBufferSize(&(tcp->super));
		gsize outSize = socket_getOutputBufferSize(&(tcp->super));
		utility_assert(16777216 > inSize);
		utility_assert(16777216 > outSize);
		socket_setInputBufferSize(&(tcp->super), (gsize) 16777216);
		socket_setOutputBufferSize(&(tcp->super), (gsize) 16777216);
		tcp->info.rtt = G_MAXUINT32;
		debug("set loopback buffer sizes to 16777216");
		return;
	}

	Address* srcAddress = dns_resolveIPToAddress(worker_getDNS(), sourceIP);
	GQuark sourceID = (GQuark)address_getID(srcAddress);
	Address* dstAddress = dns_resolveIPToAddress(worker_getDNS(), destinationIP);
	GQuark destinationID = (GQuark)address_getID(dstAddress);

	/* get latency in milliseconds */
	guint32 send_latency = (guint32) ceil(worker_getLatency(sourceID, destinationID));
	guint32 receive_latency = (guint32) ceil(worker_getLatency(destinationID, sourceID));
	if(send_latency == 0 || receive_latency == 0) {
	  error("autotuning needs nonzero latency, source=%"G_GUINT32_FORMAT" dest=%"G_GUINT32_FORMAT" send=%"G_GUINT32_FORMAT" recv=%"G_GUINT32_FORMAT,
			  sourceID, destinationID, send_latency, receive_latency);
	}
	utility_assert(send_latency > 0 && receive_latency > 0);

	guint32 rtt_milliseconds = send_latency + receive_latency;
	tcp->info.rtt = rtt_milliseconds;

	/* i got delay, now i need values for my send and receive buffer
	 * sizes based on bandwidth in both directions. do my send size first. */
	guint32 my_send_bw = worker_getNodeBandwidthUp(sourceID, sourceIP);
	guint32 their_receive_bw = worker_getNodeBandwidthDown(destinationID, destinationIP);

	/* KiBps is the same as Bpms, which works with our RTT calculation. */
	guint32 send_bottleneck_bw = my_send_bw < their_receive_bw ? my_send_bw : their_receive_bw;

	/* the delay bandwidth product is how many bytes I can send at once to keep the pipe full */
	guint64 sendbuf_size = (guint64) ((rtt_milliseconds * send_bottleneck_bw * 1024.0f * 1.25f) / 1000.0f);

	/* now the same thing for my receive buf */
	guint32 my_receive_bw = worker_getNodeBandwidthDown(sourceID, sourceIP);
	guint32 their_send_bw = worker_getNodeBandwidthUp(destinationID, destinationIP);

	/* KiBps is the same as Bpms, which works with our RTT calculation. */
	guint32 receive_bottleneck_bw = my_receive_bw < their_send_bw ? my_receive_bw : their_send_bw;

	/* the delay bandwidth product is how many bytes I can receive at once to keep the pipe full */
	guint64 receivebuf_size = (guint64) ((rtt_milliseconds * receive_bottleneck_bw * 1024.0f * 1.25f) / 1000.0f);

    /* keep minimum buffer size bounds */
    if(sendbuf_size < CONFIG_SEND_BUFFER_MIN_SIZE) {
    	sendbuf_size = CONFIG_SEND_BUFFER_MIN_SIZE;
    }
    if(receivebuf_size < CONFIG_RECV_BUFFER_MIN_SIZE) {
        receivebuf_size = CONFIG_RECV_BUFFER_MIN_SIZE;
    }

	/* make sure the user hasnt already written to the buffer, because if we
	 * shrink it, our buffer math would overflow the size variable
	 */
	utility_assert(socket_getInputBufferLength(&(tcp->super)) == 0);
	utility_assert(socket_getOutputBufferLength(&(tcp->super)) == 0);

	/* check to see if the node should set buffer sizes via autotuning, or
	 * they were specified by configuration or parameters in XML */
	Host* node = worker_getCurrentHost();
	if(host_autotuneReceiveBuffer(node)) {
		socket_setInputBufferSize(&(tcp->super), (gsize) receivebuf_size);
	}
	if(host_autotuneSendBuffer(node)) {
		tcp->super.outputBufferSize = sendbuf_size;
		socket_setOutputBufferSize(&(tcp->super), (gsize) sendbuf_size);
	}

	info("network buffer sizes: send %"G_GSIZE_FORMAT" receive %"G_GSIZE_FORMAT,
			socket_getOutputBufferSize(&(tcp->super)), socket_getInputBufferSize(&(tcp->super)));
}

static void _tcp_setState(TCP* tcp, enum TCPState state) {
	MAGIC_ASSERT(tcp);

	tcp->stateLast = tcp->state;
	tcp->state = state;

	debug("%s <-> %s: moved from TCP state '%s' to '%s'", tcp->super.boundString, tcp->super.peerString,
			tcp_stateToAscii(tcp->stateLast), tcp_stateToAscii(tcp->state));

	/* some state transitions require us to update the descriptor status */
	switch (state) {
		case TCPS_LISTEN: {
			descriptor_adjustStatus((Descriptor*)tcp, DS_ACTIVE, TRUE);
			break;
		}
		case TCPS_SYNSENT: {
			break;
		}
		case TCPS_SYNRECEIVED: {
			break;
		}
		case TCPS_ESTABLISHED: {
			tcp->flags |= TCPF_WAS_ESTABLISHED;
			if(tcp->state != tcp->stateLast) {
				_tcp_autotune(tcp);
			}
			descriptor_adjustStatus((Descriptor*)tcp, DS_ACTIVE|DS_WRITABLE, TRUE);
			break;
		}
		case TCPS_CLOSING: {
			break;
		}
		case TCPS_CLOSEWAIT: {
			break;
		}
		case TCPS_CLOSED: {
			/* user can no longer use socket */
			descriptor_adjustStatus((Descriptor*)tcp, DS_ACTIVE, FALSE);

			/*
			 * servers have to wait for all children to close.
			 * children need to notify their parents when closing.
			 */
			if(!tcp->server || g_hash_table_size(tcp->server->children) <= 0) {
				if(tcp->child && tcp->child->parent) {
					TCP* parent = tcp->child->parent;

					/* tell my server to stop accepting packets for me
					 * this will destroy the child and NULL out tcp->child */
					g_hash_table_remove(tcp->child->parent->server->children, (gconstpointer)&(tcp->child->key));

					/* if i was the server's last child and its waiting to close, close it */
					utility_assert(parent->server);
					if((parent->state == TCPS_CLOSED) && (g_hash_table_size(parent->server->children) <= 0)) {
						/* this will unbind from the network interface and free socket */
						host_closeDescriptor(worker_getCurrentHost(), parent->super.super.super.handle);
					}
				}

				/* this will unbind from the network interface and free socket */
				host_closeDescriptor(worker_getCurrentHost(), tcp->super.super.super.handle);
			}
			break;
		}
		case TCPS_TIMEWAIT: {
			/* schedule a close timer self-event to finish out the closing process */
			TCPCloseTimerExpiredEvent* event = tcpclosetimerexpired_new(tcp);
			worker_scheduleEvent((Event*)event, CONFIG_TCPCLOSETIMER_DELAY, 0);
			break;
		}
		default:
			break;
	}
}

static void _tcp_updateReceiveWindow(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	/* the receive window is how much we are willing to accept to our input buffer */
	gsize space = socket_getInputBufferSpace(&(tcp->super));
	gsize nPackets = space / (CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH);
	tcp->receive.window = nPackets;

	/* handle window updates */
	if(tcp->receive.window == 0) {
		/* we must ensure that we never advertise a 0 window if there is no way
		 * for the client to drain the input buffer to further open the window.
		 * otherwise, we may get into a deadlock situation where we never accept
		 * any packets and the client never reads. */
		utility_assert(!(socket_getInputBufferLength(&(tcp->super)) == 0));
		info("%s <-> %s: receive window is 0, we have space for %"G_GSIZE_FORMAT" bytes in the input buffer",
				tcp->super.boundString, tcp->super.peerString, space);
	}
}

static void _tcp_updateSendWindow(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	/* send window is minimum of congestion window and the last advertised window */
	tcp->send.window = MIN(tcp->congestion.window, tcp->congestion.lastWindow);
}

/* nPacketsAcked == 0 means a congestion event (packet was dropped) */
static void _tcp_updateCongestionWindow(TCP* tcp, guint nPacketsAcked) {
	MAGIC_ASSERT(tcp);

	if(nPacketsAcked > 0) {
		if(tcp->isSlowStart) {
			/* threshold not set => no timeout yet => slow start phase 1
			 *  i.e. multiplicative increase until retransmit event (which sets threshold)
			 * threshold set => timeout => slow start phase 2
			 *  i.e. multiplicative increase until threshold */
			tcp->congestion.window += ((guint32)nPacketsAcked);
			if(tcp->congestion.threshold != 0 && tcp->congestion.window >= tcp->congestion.threshold) {
				tcp->isSlowStart = FALSE;
			}
		} else {
			/* slow start is over
			 * simple additive increase part of AIMD */
			gdouble n = ((gdouble) nPacketsAcked);
			gdouble increment = n * n / ((gdouble) tcp->congestion.window);
			tcp->congestion.window += (guint32)(ceil(increment));
		}
	} else {
		/* a packet was "dropped" - this is basically a negative ack.
		 * TCP-Reno-like fast retransmit, i.e. multiplicative decrease. */
		tcp->congestion.window = (guint32) ceil((gdouble)tcp->congestion.window / (gdouble)2);

		if(tcp->isSlowStart && tcp->congestion.threshold == 0) {
			tcp->congestion.threshold = tcp->congestion.window;
		}
	}

	/* unlike the send and receive/advertised windows, our cong window should never be 0
	 *
	 * from https://tools.ietf.org/html/rfc5681 [page 6]:
	 *
	 * "Implementation Note: Since integer arithmetic is usually used in TCP
   	 *  implementations, the formula given in equation (3) can fail to
   	 *  increase cwnd when the congestion window is larger than SMSS*SMSS.
   	 *  If the above formula yields 0, the result SHOULD be rounded up to 1 byte."
	 */
	if(tcp->congestion.window == 0) {
		tcp->congestion.window = 1;
	}
}

static Packet* _tcp_createPacket(TCP* tcp, enum ProtocolTCPFlags flags, gconstpointer payload, gsize payloadLength) {
	MAGIC_ASSERT(tcp);

	/*
	 * packets from children of a server must appear to be coming from the server
	 */
	in_addr_t sourceIP = tcp_getIP(tcp);
	in_port_t sourcePort = (tcp->child) ? tcp->child->parent->super.boundPort :
			tcp->super.boundPort;

	in_addr_t destinationIP = tcp_getPeerIP(tcp);
	in_port_t destinationPort = (tcp->server) ? tcp->server->lastPeerPort : tcp->super.peerPort;

	if(sourceIP == htonl(INADDR_ANY)) {
		/* source interface depends on destination */
		if(destinationIP == htonl(INADDR_LOOPBACK)) {
			sourceIP = htonl(INADDR_LOOPBACK);
		} else {
			sourceIP = host_getDefaultIP(worker_getCurrentHost());
		}
	}

	utility_assert(sourceIP && sourcePort && destinationIP && destinationPort);

	/* make sure our receive window is up to date before putting it in the packet */
	_tcp_updateReceiveWindow(tcp);

	/* control packets have no sequence number
	 * (except FIN, so we close after sending everything) */
	guint sequence = ((payloadLength > 0) || (flags & PTCP_FIN)) ? tcp->send.next : 0;

	/* create the TCP packet */
	Packet* packet = packet_new(payload, payloadLength);
	packet_setTCP(packet, flags, sourceIP, sourcePort, destinationIP, destinationPort,
			sequence, tcp->receive.next, tcp->receive.window);

	/* update sequence number */
	if(sequence > 0) {
		tcp->send.next++;
	}

	return packet;
}

static gsize _tcp_getBufferSpaceOut(TCP* tcp) {
	MAGIC_ASSERT(tcp);
	/* account for throttled and retransmission buffer */
	gssize s = (gssize)(socket_getOutputBufferSpace(&(tcp->super)) - tcp->throttledOutputLength - tcp->retransmissionLength);
	gsize space = (gsize) MAX(0, s);
	return space;
}

static void _tcp_bufferPacketOut(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);

	/* TCP wants to avoid congestion */
	g_queue_insert_sorted(tcp->throttledOutput, packet, (GCompareDataFunc)packet_compareTCPSequence, NULL);
	tcp->throttledOutputLength += packet_getPayloadLength(packet);
	if(_tcp_getBufferSpaceOut(tcp) == 0) {
		descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, FALSE);
	}
}

static gsize _tcp_getBufferSpaceIn(TCP* tcp) {
	MAGIC_ASSERT(tcp);
	/* account for unordered input buffer */
	gssize space = (gssize)(socket_getInputBufferSpace(&(tcp->super)) - tcp->unorderedInputLength);
	return MAX(0, space);
}

static void _tcp_bufferPacketIn(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);

	/* TCP wants in-order data */
	g_queue_insert_sorted(tcp->unorderedInput, packet, (GCompareDataFunc)packet_compareTCPSequence, NULL);
	tcp->unorderedInputLength += packet_getPayloadLength(packet);
}

static void _tcp_addRetransmit(TCP* tcp, guint sequence, guint length) {
	MAGIC_ASSERT(tcp);
	g_hash_table_replace(tcp->retransmission, GINT_TO_POINTER(sequence), GINT_TO_POINTER(length));
	tcp->retransmissionLength += length;
	if(_tcp_getBufferSpaceOut(tcp) == 0) {
		descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, FALSE);
	}
}

static void _tcp_removeRetransmit(TCP* tcp, guint sequence) {
	MAGIC_ASSERT(tcp);
	gpointer key = GINT_TO_POINTER(sequence);
	if(g_hash_table_contains(tcp->retransmission, key)) {
		/* update buffer lengths */
		guint length = ((guint)GPOINTER_TO_INT(g_hash_table_lookup(tcp->retransmission, key)));
		if(length) {
			tcp->retransmissionLength -= length;
			g_hash_table_remove(tcp->retransmission, key);
			if(_tcp_getBufferSpaceOut(tcp) > 0) {
				descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, TRUE);
			}
		}
	}
}

static void _tcp_flush(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	/* make sure our information is up to date */
	_tcp_updateReceiveWindow(tcp);
	_tcp_updateSendWindow(tcp);

	/* flush packets that can now be sent to socket */
	while(g_queue_get_length(tcp->throttledOutput) > 0) {
		/* get the next throttled packet, in sequence order */
		Packet* packet = g_queue_pop_head(tcp->throttledOutput);

		/* break out if we have no packets left */
		if(!packet) {
			break;
		}

		guint length = packet_getPayloadLength(packet);

		if(length > 0) {
			PacketTCPHeader header;
			packet_getTCPHeader(packet, &header);

			/* we cant send it if our window is too small */
			gboolean fitsInWindow = (header.sequence < (tcp->send.unacked + tcp->send.window)) ? TRUE : FALSE;

			/* we cant send it if we dont have enough space */
			gboolean fitsInBuffer = (length <= socket_getOutputBufferSpace(&(tcp->super))) ? TRUE : FALSE;

			if(!fitsInBuffer || !fitsInWindow) {
				/* we cant send the packet yet */
				g_queue_push_head(tcp->throttledOutput, packet);
				break;
			} else {
				/* we will send: store length in virtual retransmission buffer
				 * so we can reduce buffer space consumed when we receive the ack */
				_tcp_addRetransmit(tcp, header.sequence, length);
				tcp->info.lastDataSent = worker_getCurrentTime();
			}
		}

		/* packet is sendable, we removed it from out buffer */
		tcp->throttledOutputLength -= length;

		/* update TCP header to our current advertised window and acknowledgement */
		packet_updateTCP(packet, tcp->receive.next, tcp->receive.window);

		/* keep track of the last things we sent them */
		tcp->send.lastAcknowledgement = tcp->receive.next;
		tcp->send.lastWindow = tcp->receive.window;
		tcp->info.lastAckSent = worker_getCurrentTime();

		 /* socket will queue it ASAP */
		gboolean success = socket_addToOutputBuffer(&(tcp->super), packet);

		/* we already checked for space, so this should always succeed */
		utility_assert(success);
	}

	/* any packets now in order can be pushed to our user input buffer */
	while(g_queue_get_length(tcp->unorderedInput) > 0) {
		Packet* packet = g_queue_pop_head(tcp->unorderedInput);

		PacketTCPHeader header;
		packet_getTCPHeader(packet, &header);

		if(header.sequence == tcp->receive.next) {
			/* move from the unordered buffer to user input buffer */
			gboolean fitInBuffer = socket_addToInputBuffer(&(tcp->super), packet);

			if(fitInBuffer) {
				tcp->unorderedInputLength -= packet_getPayloadLength(packet);
				(tcp->receive.next)++;
				continue;
			}
		}

		/* we could not buffer it because its out of order or we have no space */
		g_queue_push_head(tcp->unorderedInput, packet);
		break;
	}

	/* update the tracker input/output buffer stats */
	Tracker* tracker = host_getTracker(worker_getCurrentHost());
	Socket* socket = (Socket* )tcp;
	Descriptor* descriptor = (Descriptor *)socket;
	gsize inSize = socket_getInputBufferSize(&(tcp->super));
	gsize outSize = socket_getOutputBufferSize(&(tcp->super));
	tracker_updateSocketInputBuffer(tracker, descriptor->handle, inSize - _tcp_getBufferSpaceIn(tcp), inSize);
	tracker_updateSocketOutputBuffer(tracker, descriptor->handle, outSize - _tcp_getBufferSpaceOut(tcp), outSize);

	/* check if user needs an EOF signal */
	gboolean wantsEOF = ((tcp->flags & TCPF_LOCAL_CLOSED) || (tcp->flags & TCPF_REMOTE_CLOSED)) ? TRUE : FALSE;
	if(wantsEOF) {
		/* if anyone closed, can't send anymore */
		tcp->error |= TCPE_SEND_EOF;

		if((tcp->receive.next >= tcp->receive.end) && !(tcp->flags & TCPF_EOF_SIGNALED)) {
			/* user needs to read a 0 so it knows we closed */
			tcp->error |= TCPE_RECEIVE_EOF;
			descriptor_adjustStatus((Descriptor*)tcp, DS_READABLE, TRUE);
		}
	}

	if(_tcp_getBufferSpaceOut(tcp) > 0) {
		descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, TRUE);
	} else {
		descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, FALSE);
	}
}

gboolean tcp_isFamilySupported(TCP* tcp, sa_family_t family) {
	MAGIC_ASSERT(tcp);
	return family == AF_INET ? TRUE : FALSE;
}

gint tcp_getConnectError(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	if(tcp->error & TCPE_CONNECTION_RESET) {
		tcp->flags |= TCPF_RESET_SIGNALED;
		if(tcp->flags & TCPF_WAS_ESTABLISHED) {
			return ECONNRESET;
		} else {
			return ECONNREFUSED;
		}
	} else if(tcp->state == TCPS_SYNSENT || tcp->state == TCPS_SYNRECEIVED) {
		return EALREADY;
	} else if(tcp->flags & TCPF_EOF_SIGNALED) {
		/* we already signaled close, now its an error */
		return ENOTCONN;
	} else if(tcp->state != TCPS_CLOSED) {
		/* @todo: this affects ability to connect. if a socket is closed, can
		 * we start over and connect again? (reuseaddr socket opt)
		 * if so, this should change
		 */
		return EISCONN;
	}

	return 0;
}

static guint8 _tcp_getTCPInfoState(TCP* tcp) {
    switch(tcp->state) {
		case TCPS_ESTABLISHED: return (guint8) TCP_ESTABLISHED;
		case TCPS_SYNSENT: return (guint) TCP_SYN_SENT;
		case TCPS_SYNRECEIVED: return (guint) TCP_SYN_RECV;
		case TCPS_FINWAIT1: return (guint) TCP_FIN_WAIT1;
		case TCPS_FINWAIT2: return (guint) TCP_FIN_WAIT2;
		case TCPS_TIMEWAIT: return (guint) TCP_TIME_WAIT;
		case TCPS_CLOSED: return (guint) TCP_CLOSE;
		case TCPS_CLOSEWAIT: return (guint) TCP_CLOSE_WAIT;
		case TCPS_LASTACK: return (guint) TCP_LAST_ACK;
		case TCPS_LISTEN: return (guint) TCP_LISTEN;
		case TCPS_CLOSING: return (guint) TCP_CLOSING;
		default: return (guint8) 0;
    }
}

void tcp_getInfo(TCP* tcp, struct tcp_info *tcpinfo) {
	MAGIC_ASSERT(tcp);

	memset(tcpinfo, 0, sizeof(struct tcp_info));

	tcpinfo->tcpi_state = (u_int8_t) _tcp_getTCPInfoState(tcp);
//	tcpinfo->tcpi_ca_state;
//	tcpinfo->tcpi_retransmits;
//	tcpinfo->tcpi_probes;
//	tcpinfo->tcpi_backoff;
//	tcpinfo->tcpi_options;
//	tcpinfo->tcpi_snd_wscale;
//	tcpinfo->tcpi_rcv_wscale;

//	tcpinfo->tcpi_rto;
//	tcpinfo->tcpi_ato;
	tcpinfo->tcpi_snd_mss = (u_int32_t)(CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH);
	tcpinfo->tcpi_rcv_mss = (u_int32_t)(CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH);

	tcpinfo->tcpi_unacked = tcp->send.next - tcp->send.unacked;
//	tcpinfo->tcpi_sacked;
//	tcpinfo->tcpi_lost;
	tcpinfo->tcpi_retrans = (u_int32_t) tcp->info.retransmitCount;
//	tcpinfo->tcpi_fackets;

	/* Times. */
	tcpinfo->tcpi_last_data_sent = (u_int32_t)(tcp->info.lastDataSent/SIMTIME_ONE_MICROSECOND);
	tcpinfo->tcpi_last_ack_sent = (u_int32_t)(tcp->info.lastAckSent/SIMTIME_ONE_MICROSECOND);
	tcpinfo->tcpi_last_data_recv = (u_int32_t)(tcp->info.lastDataReceived/SIMTIME_ONE_MICROSECOND);
	tcpinfo->tcpi_last_ack_recv = (u_int32_t)(tcp->info.lastAckReceived/SIMTIME_ONE_MICROSECOND);

	/* Metrics. */
	tcpinfo->tcpi_pmtu = (u_int32_t)(CONFIG_MTU);
//	tcpinfo->tcpi_rcv_ssthresh;
	tcpinfo->tcpi_rtt = tcp->info.rtt;
//	tcpinfo->tcpi_rttvar;
	tcpinfo->tcpi_snd_ssthresh = tcp->congestion.threshold;
	tcpinfo->tcpi_snd_cwnd = tcp->congestion.window;
	tcpinfo->tcpi_advmss = (u_int32_t)(CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH);
//	tcpinfo->tcpi_reordering;

	tcpinfo->tcpi_rcv_rtt = tcp->info.rtt;
	tcpinfo->tcpi_rcv_space = tcp->congestion.lastWindow;

//	tcpinfo->tcpi_total_retrans;
}


gint tcp_connectToPeer(TCP* tcp, in_addr_t ip, in_port_t port, sa_family_t family) {
	MAGIC_ASSERT(tcp);

	/* create the connection state */
	socket_setPeerName(&(tcp->super), ip, port);

	/* send 1st part of 3-way handshake, state->syn_sent */
	Packet* packet = _tcp_createPacket(tcp, PTCP_SYN, NULL, 0);

	/* dont have to worry about space since this has no payload */
	_tcp_bufferPacketOut(tcp, packet);
	_tcp_flush(tcp);

	debug("%s <-> %s: user initiated connection", tcp->super.boundString, tcp->super.peerString);
	_tcp_setState(tcp, TCPS_SYNSENT);

	/* we dont block, so return EINPROGRESS while waiting for establishment */
	return EINPROGRESS;
}

void tcp_enterServerMode(TCP* tcp, gint backlog) {
	MAGIC_ASSERT(tcp);

	/* we are a server ready to listen, build our server state */
	tcp->server = _tcpserver_new(backlog);

	/* we are now listening for connections */
	_tcp_setState(tcp, TCPS_LISTEN);
}

gint tcp_acceptServerPeer(TCP* tcp, in_addr_t* ip, in_port_t* port, gint* acceptedHandle) {
	MAGIC_ASSERT(tcp);
	utility_assert(acceptedHandle);

	/* make sure we are listening and bound to an ip and port */
	if(tcp->state != TCPS_LISTEN || !(tcp->super.flags & SF_BOUND)) {
		return EINVAL;
	}

	/* we must be a server to accept child connections */
	if(tcp->server == NULL){
		return EINVAL;
	}

	/* if there are no pending connection ready to accept, dont block waiting */
	if(g_queue_get_length(tcp->server->pending) <= 0) {
		return EWOULDBLOCK;
	}

	/* double check the pending child before its accepted */
	TCPChild* child = g_queue_pop_head(tcp->server->pending);
	if(!child || (child->tcp && child->tcp->error == TCPE_CONNECTION_RESET)) {
		return ECONNABORTED;
	}

	MAGIC_ASSERT(child);
	utility_assert(child->tcp);

	/* better have a peer if we are established */
	utility_assert(child->tcp->super.peerIP && child->tcp->super.peerPort);

	/* child now gets "accepted" */
	child->state = TCPCS_ACCEPTED;

	/* update child descriptor status */
	descriptor_adjustStatus(&(child->tcp->super.super.super), DS_ACTIVE|DS_WRITABLE, TRUE);

	/* update server descriptor status */
	if(g_queue_get_length(tcp->server->pending) > 0) {
		descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, TRUE);
	} else {
		descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, FALSE);
	}

	*acceptedHandle = child->tcp->super.super.super.handle;
	if(ip) {
		*ip = child->tcp->super.peerIP;
	}
	if(port) {
		*port = child->tcp->super.peerPort;
	}

	return 0;
}

static TCP* _tcp_getSourceTCP(TCP* tcp, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(tcp);

	/* servers may have children keyed by ip:port */
	if(tcp->server) {
		MAGIC_ASSERT(tcp->server);

		/* children are multiplexed based on remote ip and port */
		guint childKey = utility_ipPortHash(ip, port);
		TCPChild* child = g_hash_table_lookup(tcp->server->children, &childKey);

		if(child) {
			return child->tcp;
		}
	}

	return tcp;
}

/* return TRUE if the packet should be retransmitted */
gboolean tcp_processPacket(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);

	/* fetch the TCP info from the packet */
	PacketTCPHeader header;
	packet_getTCPHeader(packet, &header);
	guint packetLength = packet_getPayloadLength(packet);

	/* if we run a server, the packet could be for an existing child */
	tcp = _tcp_getSourceTCP(tcp, header.sourceIP, header.sourcePort);

	/* now we have the true TCP for the packet */
	MAGIC_ASSERT(tcp);

	/* print packet info for debugging */
	debug("%s <-> %s: processing packet# %u length %u",
			tcp->super.boundString, tcp->super.peerString, header.sequence, packetLength);

	/* if packet is reset, don't process */
	if(header.flags & PTCP_RST) {
		/* @todo: not sure if this is handled correctly */
		debug("received RESET packet");

		if(!(tcp->state & TCPS_LISTEN) && !(tcp->error & TCPE_CONNECTION_RESET)) {
			tcp->error |= TCPE_CONNECTION_RESET;
			tcp->flags |= TCPF_REMOTE_CLOSED;

			_tcp_setState(tcp, TCPS_TIMEWAIT);

			/* it will send no more user data after what we have now */
			tcp->receive.end = tcp->receive.next;
		}

		packet_unref(packet);
		return FALSE;
	}

	/* if we are a server, we have to remember who we got this from so we can
	 * respond back to them. this is because we could be bound to several
	 * interfaces and otherwise cant decide which to send on.
	 */
	if(tcp->server) {
		tcp->server->lastPeerIP = header.sourceIP;
		tcp->server->lastPeerPort = header.sourcePort;
		tcp->server->lastIP = header.destinationIP;
	}

	/* go through the state machine, tracking processing and response */
	gboolean wasProcessed = FALSE;
	enum ProtocolTCPFlags responseFlags = PTCP_NONE;

	switch(tcp->state) {
		case TCPS_LISTEN: {
			/* receive SYN, send SYNACK, move to SYNRECEIVED */
			if(header.flags & PTCP_SYN) {
				MAGIC_ASSERT(tcp->server);
				wasProcessed = TRUE;

				/* we need to multiplex a new child */
				Host* node = worker_getCurrentHost();
				gint multiplexedHandle = host_createDescriptor(node, DT_TCPSOCKET);
				TCP* multiplexed = (TCP*) host_lookupDescriptor(node, multiplexedHandle);

				multiplexed->child = _tcpchild_new(multiplexed, tcp, header.sourceIP, header.sourcePort);
				utility_assert(g_hash_table_lookup(tcp->server->children, &(multiplexed->child->key)) == NULL);
				g_hash_table_replace(tcp->server->children, &(multiplexed->child->key), multiplexed->child);

				multiplexed->receive.start = header.sequence;
				multiplexed->receive.next = multiplexed->receive.start + 1;

				debug("%s <-> %s: server multiplexed child socket %s <-> %s",
						tcp->super.boundString, tcp->super.peerString,
						multiplexed->super.boundString, multiplexed->super.peerString);

				_tcp_setState(multiplexed, TCPS_SYNRECEIVED);

				/* parent will send response */
				responseFlags = PTCP_SYN|PTCP_ACK;
			}
			break;
		}

		case TCPS_SYNSENT: {
			/* receive SYNACK, send ACK, move to ESTABLISHED */
			if((header.flags & PTCP_SYN) && (header.flags & PTCP_ACK)) {
				wasProcessed = TRUE;
				tcp->receive.start = header.sequence;
				tcp->receive.next = tcp->receive.start + 1;

				responseFlags |= PTCP_ACK;
				_tcp_setState(tcp, TCPS_ESTABLISHED);
			}
			/* receive SYN, send ACK, move to SYNRECEIVED (simultaneous open) */
			else if(header.flags & PTCP_SYN) {
				wasProcessed = TRUE;
				tcp->receive.start = header.sequence;
				tcp->receive.next = tcp->receive.start + 1;

				responseFlags |= PTCP_ACK;
				_tcp_setState(tcp, TCPS_SYNRECEIVED);
			}

			break;
		}

		case TCPS_SYNRECEIVED: {
			/* receive ACK, move to ESTABLISHED */
			if(header.flags & PTCP_ACK) {
				wasProcessed = TRUE;
				_tcp_setState(tcp, TCPS_ESTABLISHED);

				/* if this is a child, mark it accordingly */
				if(tcp->child) {
					tcp->child->state = TCPCS_PENDING;
					g_queue_push_tail(tcp->child->parent->server->pending, tcp->child);
					/* user should accept new child from parent */
					descriptor_adjustStatus(&(tcp->child->parent->super.super.super), DS_READABLE, TRUE);
				}
			}
			break;
		}

		case TCPS_ESTABLISHED: {
			/* receive FIN, send FINACK, move to CLOSEWAIT */
			if(header.flags & PTCP_FIN) {
				wasProcessed = TRUE;

				/* other side of connections closed */
				tcp->flags |= TCPF_REMOTE_CLOSED;
				responseFlags |= (PTCP_FIN|PTCP_ACK);
				_tcp_setState(tcp, TCPS_CLOSEWAIT);

				/* remote will send us no more user data after this sequence */
				tcp->receive.end = header.sequence;
			}
			break;
		}

		case TCPS_FINWAIT1: {
			/* receive FINACK, move to FINWAIT2 */
			if((header.flags & PTCP_FIN) && (header.flags & PTCP_ACK)) {
				wasProcessed = TRUE;
				_tcp_setState(tcp, TCPS_FINWAIT2);
			}
			/* receive FIN, send FINACK, move to CLOSING (simultaneous close) */
			else if(header.flags & PTCP_FIN) {
				wasProcessed = TRUE;
				responseFlags |= (PTCP_FIN|PTCP_ACK);
				tcp->flags |= TCPF_REMOTE_CLOSED;
				_tcp_setState(tcp, TCPS_CLOSING);

				/* it will send no more user data after this sequence */
				tcp->receive.end = header.sequence;
			}
			break;
		}

		case TCPS_FINWAIT2: {
			/* receive FIN, send FINACK, move to TIMEWAIT */
			if(header.flags & PTCP_FIN) {
				wasProcessed = TRUE;
				responseFlags |= (PTCP_FIN|PTCP_ACK);
				tcp->flags |= TCPF_REMOTE_CLOSED;
				_tcp_setState(tcp, TCPS_TIMEWAIT);

				/* it will send no more user data after this sequence */
				tcp->receive.end = header.sequence;
			}
			break;
		}

		case TCPS_CLOSING: {
			/* receive FINACK, move to TIMEWAIT */
			if((header.flags & PTCP_FIN) && (header.flags & PTCP_ACK)) {
				wasProcessed = TRUE;
				_tcp_setState(tcp, TCPS_TIMEWAIT);
			}
			break;
		}

		case TCPS_TIMEWAIT: {
			break;
		}

		case TCPS_CLOSEWAIT: {
			break;
		}

		case TCPS_LASTACK: {
			/* receive FINACK, move to CLOSED */
			if((header.flags & PTCP_FIN) && (header.flags & PTCP_ACK)) {
				wasProcessed = TRUE;
				_tcp_setState(tcp, TCPS_CLOSED);
				/* we closed, cant use tcp anymore, no retransmit */
				packet_unref(packet);
				return FALSE;
			}
			break;
		}

		case TCPS_CLOSED: {
			/* stray packet, drop without retransmit */
			packet_unref(packet);
			return FALSE;
			break;
		}

		default: {
			break;
		}

	}

	gint nPacketsAcked = 0;

	/* check if we can update some TCP control info */
	if(header.flags & PTCP_ACK) {
		wasProcessed = TRUE;
		if((header.acknowledgement > tcp->send.unacked) && (header.acknowledgement <= tcp->send.next)) {
			/* some data we sent got acknowledged */
			nPacketsAcked = header.acknowledgement - tcp->send.unacked;

			/* the packets just acked are 'released' from retransmit queue */
			for(guint i = tcp->send.unacked; i < header.acknowledgement; i++) {
				_tcp_removeRetransmit(tcp, i);
			}

			tcp->send.unacked = header.acknowledgement;

			/* update congestion window and keep track of when it was updated */
			tcp->congestion.lastWindow = (guint32) header.window;
			tcp->congestion.lastSequence = (guint32) header.sequence;
			tcp->congestion.lastAcknowledgement = (guint32) header.acknowledgement;
		}

		/* if this is a dup ack, take the new advertised window if it opened */
		if(tcp->congestion.lastAcknowledgement == (guint32) header.acknowledgement &&
				tcp->congestion.lastWindow < (guint32) header.window &&
				header.sequence == 0) {
			/* other end is telling us that its window opened and we can send more */
			tcp->congestion.lastWindow = (guint32) header.window;
		}

		tcp->info.lastAckReceived = worker_getCurrentTime();
	}

	gboolean doRetransmitData = FALSE;

	/* check if the packet carries user data for us */
	if(packetLength > 0) {
		/* it has data, check if its in the correct range */
		if(header.sequence >= (tcp->receive.next + tcp->receive.window)) {
			/* its too far ahead to accept now, but they should re-send it */
			wasProcessed = TRUE;
			doRetransmitData = TRUE;

		} else if(header.sequence >= tcp->receive.next) {
			/* its in our window, so we can accept the data */
			wasProcessed = TRUE;

			/*
			 * if this is THE next packet, we MUST accept it to avoid
			 * deadlocks (unless we are blocked b/c user should read)
			 */
			gboolean isNextPacket = (header.sequence == tcp->receive.next) ? TRUE : FALSE;
			gboolean packetFits = (packetLength <= _tcp_getBufferSpaceIn(tcp)) ? TRUE : FALSE;

			DescriptorStatus s = descriptor_getStatus((Descriptor*) tcp);
			gboolean waitingUserRead = (s & DS_READABLE) ? TRUE : FALSE;
			
			if((isNextPacket && !waitingUserRead) || (packetFits)) {
				/* make sure its in order */
				_tcp_bufferPacketIn(tcp, packet);
				tcp->info.lastDataReceived = worker_getCurrentTime();
			} else {
				debug("no space for packet even though its in our window");
				doRetransmitData = TRUE;
			}
		}
	}

	/* if it is a spurious packet, send a reset */
	if(!wasProcessed) {
		utility_assert(responseFlags == PTCP_NONE);
		responseFlags = PTCP_RST;
	}

	/* update congestion window only if we received new acks.
	 * dont update if nPacketsAcked is 0, as that denotes a congestion event */
	if(nPacketsAcked > 0) {
		_tcp_updateCongestionWindow(tcp, nPacketsAcked);
	}

	/* now flush as many packets as we can to socket */
	_tcp_flush(tcp);

	/* send ack if they need updates but we didn't send any yet (selective acks) */
	if((tcp->receive.next > tcp->send.lastAcknowledgement) ||
			(tcp->receive.window != tcp->send.lastWindow))
	{
		responseFlags |= PTCP_ACK;
	}

	/* send control packet if we have one */
	if(responseFlags != PTCP_NONE) {
		debug("%s <-> %s: sending response control packet",
				tcp->super.boundString, tcp->super.peerString);
		Packet* response = _tcp_createPacket(tcp, responseFlags, NULL, 0);
		_tcp_bufferPacketOut(tcp, response);
		_tcp_flush(tcp);
	}

	/* we should free packets that are done but were not buffered */
	if(!doRetransmitData && packetLength <= 0) {
		packet_unref(packet);
	}
	return doRetransmitData;
}

void tcp_droppedPacket(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);

	PacketTCPHeader header;
	packet_getTCPHeader(packet, &header);

	/* if we run a server, the packet could be for an existing child */
	tcp = _tcp_getSourceTCP(tcp, header.destinationIP, header.destinationPort);

	/* if we are closed, we don't care */
	if(tcp->state == TCPS_CLOSED) {
		return;
	}

	/* the packet was "dropped", handle congestion control */
	_tcp_updateCongestionWindow(tcp, 0); /* 0 means congestion event */

	debug("%s <-> %s: retransmitting packet# %u", tcp->super.boundString, tcp->super.peerString, header.sequence);

	/* buffer and send as appropriate */
	_tcp_removeRetransmit(tcp, header.sequence);
	_tcp_bufferPacketOut(tcp, packet);
	_tcp_flush(tcp);
	tcp->info.retransmitCount++;
}

static void _tcp_endOfFileSignalled(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	debug("%s <-> %s: signaling close to user, socket no longer usable", tcp->super.boundString, tcp->super.peerString);
	tcp->flags |= TCPF_EOF_SIGNALED;

	/* user can no longer access socket */
	descriptor_adjustStatus(&(tcp->super.super.super), DS_CLOSED, TRUE);
	descriptor_adjustStatus(&(tcp->super.super.super), DS_ACTIVE, FALSE);
}

gssize tcp_sendUserData(TCP* tcp, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(tcp);

	/* return 0 to signal close, if necessary */
	if(tcp->error & TCPE_SEND_EOF)
	{
		if(tcp->flags & TCPF_EOF_SIGNALED) {
			/* we already signaled close, now its an error */
			return -2;
		} else {
			/* we have not signaled close, do that now */
			_tcp_endOfFileSignalled(tcp);
			return 0;
		}
	}

	/* maximum data we can send network, o/w tcp truncates and only sends 65536*/
	gsize acceptable = MIN(nBytes, 65535);
	gsize space = _tcp_getBufferSpaceOut(tcp);
	gsize remaining = MIN(acceptable, space);

	/* break data into segments and send each in a packet */
	gsize maxPacketLength = CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH;
	gsize bytesCopied = 0;

	/* create as many packets as needed */
	while(remaining > 0) {
		gsize copyLength = MIN(maxPacketLength, remaining);

		/* use helper to create the packet */
		Packet* packet = _tcp_createPacket(tcp, PTCP_ACK, buffer + bytesCopied, copyLength);
		if(copyLength > 0) {
			/* we are sending more user data */
			tcp->send.end++;
		}

		/* buffer the outgoing packet in TCP */
		_tcp_bufferPacketOut(tcp, packet);

		remaining -= copyLength;
		bytesCopied += copyLength;
	}

	debug("%s <-> %s: sending %"G_GSIZE_FORMAT" user bytes", tcp->super.boundString, tcp->super.peerString, bytesCopied);

	/* now flush as much as possible out to socket */
	_tcp_flush(tcp);

	return (gssize) (bytesCopied == 0 ? -1 : bytesCopied);
}

gssize tcp_receiveUserData(TCP* tcp, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(tcp);

	/*
	 * TODO
	 * We call descriptor_adjustStatus too many times here, to handle the readable
	 * state of the socket at times when we have a partially read packet. Consider
	 * adding a required hook for socket subclasses so the socket layer can
	 * query TCP for readability status.
	 */

	/* make sure we pull in all readable user data */
	_tcp_flush(tcp);

	gsize remaining = nBytes;
	gsize bytesCopied = 0;
	gsize totalCopied = 0;
	gsize offset = 0;
	gsize copyLength = 0;

	/* check if we have a partial packet waiting to get finished */
	if(remaining > 0 && tcp->partialUserDataPacket) {
		guint partialLength = packet_getPayloadLength(tcp->partialUserDataPacket);
		guint partialBytes = partialLength - tcp->partialOffset;
		utility_assert(partialBytes > 0);

		copyLength = MIN(partialBytes, remaining);
		bytesCopied = packet_copyPayload(tcp->partialUserDataPacket, tcp->partialOffset, buffer, copyLength);
		totalCopied += bytesCopied;
		remaining -= bytesCopied;
		offset += bytesCopied;

		if(bytesCopied >= partialBytes) {
			/* we finished off the partial packet */
			packet_unref(tcp->partialUserDataPacket);
			tcp->partialUserDataPacket = NULL;
			tcp->partialOffset = 0;
		} else {
			/* still more partial bytes left */
			tcp->partialOffset += bytesCopied;
			utility_assert(remaining == 0);
		}
	}

	while(remaining > 0) {
		/* if we get here, we should have read the partial packet above, or
		 * broken out below */
		utility_assert(tcp->partialUserDataPacket == NULL);
		utility_assert(tcp->partialOffset == 0);

		/* get the next buffered packet - we'll always need it.
		 * this could mark the socket as unreadable if this is its last packet.*/
		Packet* packet = socket_removeFromInputBuffer((Socket*)tcp);
		if(!packet) {
			/* no more packets or partial packets */
			break;
		}

		guint packetLength = packet_getPayloadLength(packet);
		copyLength = MIN(packetLength, remaining);
		bytesCopied = packet_copyPayload(packet, 0, buffer + offset, copyLength);
		totalCopied += bytesCopied;
		remaining -= bytesCopied;
		offset += bytesCopied;

		if(bytesCopied < packetLength) {
			/* we were only able to read part of this packet */
			tcp->partialUserDataPacket = packet;
			tcp->partialOffset = bytesCopied;
			break;
		}

		/* we read the entire packet, and are now finished with it */
		packet_unref(packet);
	}

	/* now we update readability of the socket */
	if((socket_getInputBufferLength(&(tcp->super)) > 0) || (tcp->partialUserDataPacket != NULL)) {
		/* we still have readable data */
		descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, TRUE);
	} else {
		/* all of our ordered user data has been read */
		if((tcp->unorderedInputLength == 0) && (tcp->error & TCPE_RECEIVE_EOF)) {
			/* there is no more unordered data either, and we need to signal EOF */
			if(totalCopied > 0) {
				/* we just received bytes, so we can't EOF until the next call.
				 * make sure we stay readable so we DO actually EOF the socket */
				descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, TRUE);
			} else {
				/* OK, no more data and nothing just received. */
				if(tcp->flags & TCPF_EOF_SIGNALED) {
					/* we already signaled close, now its an error */
					return -2;
				} else {
					/* we have not signaled close, do that now and close out the socket */
					_tcp_endOfFileSignalled(tcp);
					return 0;
				}
			}
		} else {
			/* our socket still has unordered data or is still open, but empty for now */
			descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, FALSE);
		}
	}

	/* if we have advertised a 0 window because the application wasn't reading,
	 * we now have to update the window and let the sender know */
	_tcp_updateReceiveWindow(tcp);
	if(tcp->send.lastWindow == 0 && tcp->receive.window > 0) {
		/* our receive window just opened, make sure the sender knows it can
		 * send more. otherwise we get into a deadlock situation! */
		info("%s <-> %s: receive window opened, advertising the new "
				"receive window %"G_GUINT32_FORMAT" as an ACK control packet",
				tcp->super.boundString, tcp->super.peerString, tcp->receive.window);
		Packet* windowUpdate = _tcp_createPacket(tcp, PTCP_ACK, NULL, 0);
		_tcp_bufferPacketOut(tcp, windowUpdate);
		_tcp_flush(tcp);
	}

	debug("%s <-> %s: receiving %"G_GSIZE_FORMAT" user bytes", tcp->super.boundString, tcp->super.peerString, totalCopied);

	return (gssize) (totalCopied == 0 ? -1 : totalCopied);
}

void tcp_free(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	while(g_queue_get_length(tcp->throttledOutput) > 0) {
		packet_unref(g_queue_pop_head(tcp->throttledOutput));
	}
	g_queue_free(tcp->throttledOutput);

	while(g_queue_get_length(tcp->unorderedInput) > 0) {
		packet_unref(g_queue_pop_head(tcp->unorderedInput));
	}
	g_queue_free(tcp->unorderedInput);

	g_hash_table_destroy(tcp->retransmission);

	if(tcp->child) {
		MAGIC_ASSERT(tcp->child);
		MAGIC_ASSERT(tcp->child->parent);
		MAGIC_ASSERT(tcp->child->parent->server);

		/* remove parents reference to child, if it exists */
		g_hash_table_remove(tcp->child->parent->server->children, &(tcp->child->key));

		_tcpchild_free(tcp->child);
	}

	if(tcp->server) {
		_tcpserver_free(tcp->server);
	}

	MAGIC_CLEAR(tcp);
	g_free(tcp);
}

void tcp_close(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	debug("%s <-> %s:  user closed connection", tcp->super.boundString, tcp->super.peerString);
	tcp->flags |= TCPF_LOCAL_CLOSED;

	switch (tcp->state) {
		case TCPS_LISTEN: {
			_tcp_setState(tcp, TCPS_CLOSED);
			return;
		}

		case TCPS_ESTABLISHED: {
			_tcp_setState(tcp, TCPS_FINWAIT1);
			break;
		}

		case TCPS_CLOSEWAIT: {
			_tcp_setState(tcp, TCPS_LASTACK);
			break;
		}

		case TCPS_SYNRECEIVED:
		case TCPS_SYNSENT: {
			Packet* reset = _tcp_createPacket(tcp, PTCP_RST, NULL, 0);
			_tcp_bufferPacketOut(tcp, reset);
			_tcp_flush(tcp);
			return;
		}

		default: {
			/* dont send a FIN */
			return;
		}
	}

	/* send a FIN */
	Packet* packet = _tcp_createPacket(tcp, PTCP_FIN, NULL, 0);

	/* dont have to worry about space since this has no payload */
	_tcp_bufferPacketOut(tcp, packet);
	_tcp_flush(tcp);
}

void tcp_closeTimerExpired(TCP* tcp) {
	MAGIC_ASSERT(tcp);
	_tcp_setState(tcp, TCPS_CLOSED);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable tcp_functions = {
	(DescriptorFunc) tcp_close,
	(DescriptorFunc) tcp_free,
	(TransportSendFunc) tcp_sendUserData,
	(TransportReceiveFunc) tcp_receiveUserData,
	(SocketProcessFunc) tcp_processPacket,
	(SocketDroppedPacketFunc) tcp_droppedPacket,
	(SocketIsFamilySupportedFunc) tcp_isFamilySupported,
	(SocketConnectToPeerFunc) tcp_connectToPeer,
	MAGIC_VALUE
};

TCP* tcp_new(gint handle, guint receiveBufferSize, guint sendBufferSize) {
	TCP* tcp = g_new0(TCP, 1);
	MAGIC_INIT(tcp);

	socket_init(&(tcp->super), &tcp_functions, DT_TCPSOCKET, handle, receiveBufferSize, sendBufferSize);

	guint32 initial_window = worker_getConfig()->initialTCPWindow;

	tcp->congestion.window = initial_window;
	tcp->congestion.lastWindow = initial_window;
	tcp->send.window = initial_window;
	tcp->send.lastWindow = initial_window;
	tcp->receive.window = initial_window;

	/* 0 is saved for representing control packets */
	guint32 initialSequenceNumber = 1;

	tcp->congestion.lastSequence = initialSequenceNumber;
	tcp->congestion.lastAcknowledgement = initialSequenceNumber;
	tcp->send.unacked = initialSequenceNumber;
	tcp->send.next = initialSequenceNumber;
	tcp->send.end = initialSequenceNumber;
	tcp->send.lastAcknowledgement = initialSequenceNumber;
	tcp->receive.end = initialSequenceNumber;
	tcp->receive.next = initialSequenceNumber;
	tcp->receive.start = initialSequenceNumber;

	tcp->isSlowStart = TRUE;

	tcp->throttledOutput = g_queue_new();
	tcp->unorderedInput = g_queue_new();
	tcp->retransmission = g_hash_table_new(g_direct_hash, g_direct_equal);

	return tcp;
}
