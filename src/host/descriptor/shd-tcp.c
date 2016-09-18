/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * Copyright (c) 2013-2014, John Geddes
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
    TCPF_CONNECT_SIGNALED = 1 << 5,
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

typedef enum TCPReceiveState TCPReceiveState;
enum TCPReceiveState {
    TCPRS_OPEN = 0,
    TCPRS_RECOVERY = 1,
    TCPRS_LOSS = 2,
};

typedef struct _TCPChild TCPChild;
struct _TCPChild {
    enum TCPChildState state;
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
        /* state that the receive TCP is in (Open,Recovery,Loss) */
        TCPReceiveState state;
        /* initial receive sequence number */
        guint32 start;
        /* next packet we expect to receive */
        guint32 next;
        /* how far past next can we receive */
        guint32 window;
        /* used to make sure we get all data when other end closes */
        guint32 end;
        /* acknowledgment needed to get out of fast recovery */
        guint32 recoveryPoint;
        /* last timestamp received in timestamp value field */
        SimulationTime lastTimestamp;
        /* the last advertisements to us */
        guint32 lastWindow;
        guint32 lastAcknowledgment;
        guint32 lastSequence;
        gboolean windowUpdatePending;
        GList* lastSelectiveACKs;
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
        guint32 lastAcknowledgment;
        /* the last advertised window we sent them */
        guint32 lastWindow;
        /* highest sequence sent */
        guint32 highestSequence;
        /* total number of packets sent */
        guint32 packetsSent;
        /* list of selective ACKs, packets received after a missing packet */
        GList* selectiveACKs;
    } send;

    struct {
        /* TCP provides reliable transport, keep track of packets until they are acked */
        GHashTable* queue;
        /* track amount of queued application data */
        gsize queueLength;
        /* retransmission timeout value (rto), in milliseconds */
        gint timeout;
        /* when the scheduled timer events will expire; empty if no retransmit is scheduled */
        PriorityQueue* scheduledTimerExpirations;
        /* our updated expiration time, to determine if previous events are still valid */
        SimulationTime desiredTimerExpiration;
        /* number of times we backed off due to congestion */
        guint backoffCount;

        ScoreBoard* scoreboard;
    } retransmit;

    /* tcp autotuning for the send and recv buffers */
    struct {
        gboolean isEnabled;
        gsize bytesCopied;
        SimulationTime lastAdjustment;
        gsize space;
    } autotune;

    /* congestion object for implementing different types of congestion control (aimd, reno, cubic) */
    TCPCongestion* congestion;

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
    PriorityQueue* throttledOutput;
    /* track amount of queued application data */
    gsize throttledOutputLength;

    /* TCP ensures that the user receives data in-order */
    PriorityQueue* unorderedInput;
    /* track amount of queued application data */
    gsize unorderedInputLength;

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

    descriptor_ref(parent);
    child->parent = parent;

    child->state = TCPCS_INCOMPLETE;
    socket_setPeerName(&(tcp->super), peerIP, peerPort);

    /* the child is bound to the parent server's address, because all packets
     * coming from the child should appear to be coming from the server itself */
    in_addr_t parentAddress;
    in_port_t parentPort;
    socket_getSocketName(&(parent->super), &parentAddress, &parentPort);
    socket_setSocketName(&(tcp->super), parentAddress, parentPort, TRUE);

    return child;
}

static void _tcpchild_free(TCPChild* child) {
    MAGIC_ASSERT(child);

    descriptor_unref(child->parent);

    MAGIC_CLEAR(child);
    g_free(child);
}

static TCPServer* _tcpserver_new(gint backlog) {
    TCPServer* server = g_new0(TCPServer, 1);
    MAGIC_INIT(server);

    server->children = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify) descriptor_unref);
    server->pending = g_queue_new();
    server->pendingMaxLength = backlog;

    return server;
}

static void _tcpserver_free(TCPServer* server) {
    MAGIC_ASSERT(server);

    /* no need to destroy children in this queue */
    g_queue_free(server->pending);
    /* this will unref all children */
    if(server->children) {
        g_hash_table_destroy(server->children);
    }

    MAGIC_CLEAR(server);
    g_free(server);
}

void tcp_clearAllChildrenIfServer(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    if(tcp->server && tcp->server->children) {
        g_hash_table_destroy(tcp->server->children);
        tcp->server->children = NULL;
    }
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

static guint _tcp_calculateRTT(TCP* tcp) {
    MAGIC_ASSERT(tcp);

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

    guint rtt = 1;

    if(sourceIP != destinationIP) {
        Address* srcAddress = dns_resolveIPToAddress(worker_getDNS(), sourceIP);
        Address* dstAddress = dns_resolveIPToAddress(worker_getDNS(), destinationIP);

        GQuark sourceID = (GQuark)address_getID(srcAddress);
        GQuark destinationID = (GQuark)address_getID(dstAddress);

        /* get latency in milliseconds */
        gdouble srcLatency = worker_getLatency(sourceID, destinationID);
        gdouble dstLatency = worker_getLatency(destinationID, sourceID);

        guint sendLatency = (guint) ceil(srcLatency);
        guint receiveLatency = (guint) ceil(dstLatency);

        if(sendLatency == 0 || receiveLatency == 0) {
          error("need nonzero latency to set buffer sizes, "
                  "source=%"G_GUINT32_FORMAT" dest=%"G_GUINT32_FORMAT" send=%"G_GUINT32_FORMAT" recv=%"G_GUINT32_FORMAT,
                  sourceID, destinationID, sendLatency, receiveLatency);
        }
        utility_assert(sendLatency > 0 && receiveLatency > 0);

        rtt = sendLatency + receiveLatency;
    }

    return rtt;
}

static void _tcp_setBufferSizes(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    if(!CONFIG_TCPAUTOTUNE) {
        return;
    }

    /* our buffers need to be large enough to send and receive
     * a full delay*bandwidth worth of bytes to keep the pipe full.
     * but not too large that we'll just buffer everything. autotuning
     * is meant to tune it to an optimal rate. here, we approximate that
     * by getting the true latencies instead of detecting them.
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

    guint32 rtt_milliseconds = (guint32)_tcp_calculateRTT(tcp);

    Address* srcAddress = dns_resolveIPToAddress(worker_getDNS(), sourceIP);
    Address* dstAddress = dns_resolveIPToAddress(worker_getDNS(), destinationIP);

    GQuark sourceID = (GQuark)address_getID(srcAddress);
    GQuark destinationID = (GQuark)address_getID(dstAddress);

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

    info("set network buffer sizes: send %"G_GSIZE_FORMAT" receive %"G_GSIZE_FORMAT,
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
            if(tcp->state != tcp->stateLast && !tcp->autotune.isEnabled) {
                _tcp_setBufferSizes(tcp);
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
                    utility_assert(parent->server);

                    /* tell my server to stop accepting packets for me
                     * this will destroy the child and NULL out tcp->child */
                    g_hash_table_remove(parent->server->children, &(tcp->child->key));

                    /* if i was the server's last child and its waiting to close, close it */
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
        case TCPS_LASTACK:
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

static void _tcp_autotuneReceiveBuffer(TCP* tcp, guint bytesCopied) {
    MAGIC_ASSERT(tcp);

    SimulationTime now = worker_getCurrentTime();

    tcp->autotune.bytesCopied += (gsize)bytesCopied;

    if(tcp->autotune.lastAdjustment == 0) {
        tcp->autotune.lastAdjustment = now;
        return;
    }

    SimulationTime time = now - tcp->autotune.lastAdjustment;
    SimulationTime threshold = ((SimulationTime)tcp->congestion->rttSmoothed) * ((SimulationTime)SIMTIME_ONE_MILLISECOND);

    if(tcp->congestion->rttSmoothed == 0 || (time < threshold)) {
        return;
    }

    gsize space = 2 * tcp->autotune.bytesCopied;
    space = MAX(space, tcp->autotune.space);

    gsize currentSize = socket_getInputBufferSize(&tcp->super);
    if(space > currentSize) {
        tcp->autotune.space = space;

        gsize newSize = (gsize) MIN(space, (gsize)CONFIG_TCP_RMEM_MAX);
        if(newSize > currentSize) {
            socket_setInputBufferSize(&tcp->super, newSize);
            debug("[autotune] input buffer size adjusted from %"G_GSIZE_FORMAT" to %"G_GSIZE_FORMAT,
                    currentSize, newSize);
        }
    }

    tcp->autotune.lastAdjustment = now;
    tcp->autotune.bytesCopied = 0;
}

static void _tcp_autotuneSendBuffer(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    /* Linux Kernel 3.11.6:
     *     int sndmem = SKB_TRUESIZE(max_t(u32, tp->rx_opt.mss_clamp, tp->mss_cache) + MAX_TCP_HEADER);
     *     int demanded = max_t(unsigned int, tp->snd_cwnd, tp->reordering + 1);
     *     sndmem *= 2 * demanded;
     *
     * We don't have any of the values to calculate the initial sndmem value which attempts to calculate
     * the maximum size that an MSS may be.  However, by looking at the send buffer length and cwnd values
     * of an actual download, around 66% of values were exactly 2404, while the remaining 33% were
     * 2200 <= sndmem < 2404.  For now hard code as 2404 and maybe later figure out how to calculate it
     * or sample from a distribution. */

    gsize sndmem = 2404;
    gsize demanded = (gsize)tcp->congestion->window;
    gsize newSize = (gsize) MIN((gsize)(sndmem * 2 * demanded), (gsize)CONFIG_TCP_WMEM_MAX);

    gsize currentSize = socket_getOutputBufferSize(&tcp->super);
    if(newSize > currentSize) {
        socket_setOutputBufferSize(&tcp->super, newSize);
        debug("[autotune] output buffer size adjusted from %"G_GSIZE_FORMAT" to %"G_GSIZE_FORMAT,
                currentSize, newSize);
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
    tcp->send.window = MIN(tcp->congestion->window, tcp->receive.lastWindow);
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
    gboolean isFinNotAck = ((flags & PTCP_FIN) && !(flags & PTCP_ACK));
    guint sequence = payloadLength > 0 || isFinNotAck ? tcp->send.next : 0;

    /* create the TCP packet. the ack, window, and timestamps will be set in _tcp_flush */
    Packet* packet = packet_new(payload, payloadLength);
    packet_setDropNotificationDelay(packet, (tcp->congestion->rttSmoothed * 2) * SIMTIME_ONE_MILLISECOND);
    packet_setTCP(packet, flags, sourceIP, sourcePort, destinationIP, destinationPort, sequence);
    packet_addDeliveryStatus(packet, PDS_SND_CREATED);

    /* update sequence number */
    if(sequence > 0) {
        tcp->send.next++;
    }

    return packet;
}

static gsize _tcp_getBufferSpaceOut(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    /* account for throttled and retransmission buffer */
    gssize s = (gssize)(socket_getOutputBufferSpace(&(tcp->super)) - tcp->throttledOutputLength - tcp->retransmit.queueLength);
    gsize space = (gsize) MAX(0, s);
    return space;
}

static void _tcp_bufferPacketOut(TCP* tcp, Packet* packet) {
    MAGIC_ASSERT(tcp);

    /* TCP wants to avoid congestion */
    priorityqueue_push(tcp->throttledOutput, packet);
    tcp->throttledOutputLength += packet_getPayloadLength(packet);
    packet_addDeliveryStatus(packet, PDS_SND_TCP_ENQUEUE_THROTTLED);

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
    priorityqueue_push(tcp->unorderedInput, packet);
    packet_ref(packet);
    tcp->unorderedInputLength += packet_getPayloadLength(packet);

    packet_addDeliveryStatus(packet, PDS_RCV_TCP_ENQUEUE_UNORDERED);
}

static void _tcp_addRetransmit(TCP* tcp, Packet* packet) {
    MAGIC_ASSERT(tcp);

    packet_ref(packet);

    PacketTCPHeader header;
    packet_getTCPHeader(packet, &header);
    g_hash_table_insert(tcp->retransmit.queue, GINT_TO_POINTER(header.sequence), packet);
    packet_addDeliveryStatus(packet, PDS_SND_TCP_ENQUEUE_RETRANSMIT);

    tcp->retransmit.queueLength += packet_getPayloadLength(packet);
    if(_tcp_getBufferSpaceOut(tcp) == 0) {
        descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, FALSE);
    }
}

/* remove all packets with a sequence number less than the sequence parameter */
static void _tcp_clearRetransmit(TCP* tcp, guint sequence) {
    MAGIC_ASSERT(tcp);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, tcp->retransmit.queue);

    while(g_hash_table_iter_next(&iter, &key, &value)) {
        guint ackedSequence = GPOINTER_TO_INT(key);
        Packet* ackedPacket = (Packet*)value;

        if(ackedSequence < sequence) {
            tcp->retransmit.queueLength -= packet_getPayloadLength(ackedPacket);
            packet_addDeliveryStatus(ackedPacket, PDS_SND_TCP_DEQUEUE_RETRANSMIT);
            g_hash_table_iter_remove(&iter);
        }
    }

    if(_tcp_getBufferSpaceOut(tcp) > 0) {
        descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, TRUE);
    }
}

static void _tcp_scheduleRetransmitTimer(TCP* tcp, SimulationTime now, SimulationTime delay) {
    MAGIC_ASSERT(tcp);

    SimulationTime* expireTimePtr = g_new0(SimulationTime, 1);
    *expireTimePtr = now + delay;
    gboolean success = priorityqueue_push(tcp->retransmit.scheduledTimerExpirations, expireTimePtr);

    if(success) {
        TCPRetransmitTimerExpiredEvent* event = tcpretransmittimerexpired_new(tcp);

        /* this is a local event for our own host */
        Host* host = worker_getCurrentHost();
        Address* address = host_getDefaultAddress(host);
        GQuark id = (GQuark) address_getID(address);

        worker_scheduleEvent((Event*)event, delay, id);

        debug("%s retransmit timer scheduled for %"G_GUINT64_FORMAT" ns",
                tcp->super.boundString, *expireTimePtr);
    } else {
        warning("%s could not schedule a retransmit timer for %"G_GUINT64_FORMAT" ns",
                tcp->super.boundString, *expireTimePtr);
        g_free(expireTimePtr);
    }
}

static void _tcp_scheduleRetransmitTimerIfNeeded(TCP* tcp, SimulationTime now) {
    /* logic for scheduling retransmission events. we only need to schedule one if
     * we have no events that will allow us to schedule one later. */
    SimulationTime* nextTimePtr = priorityqueue_peek(tcp->retransmit.scheduledTimerExpirations);
    if(nextTimePtr && *nextTimePtr <= tcp->retransmit.desiredTimerExpiration) {
        /* another event will fire before the RTO expires, check again then */
        return;
    }

    /* no existing timer will expire as early as desired */
    SimulationTime delay = tcp->retransmit.desiredTimerExpiration - now;
    _tcp_scheduleRetransmitTimer(tcp, now, delay);
}

static void _tcp_setRetransmitTimer(TCP* tcp, SimulationTime now) {
    MAGIC_ASSERT(tcp);

    /* our retransmission timer needs to change
     * track the new expiration time based on the current RTO */
    SimulationTime delay = tcp->retransmit.timeout * SIMTIME_ONE_MILLISECOND;
    tcp->retransmit.desiredTimerExpiration = now + delay;

    _tcp_scheduleRetransmitTimerIfNeeded(tcp, now);
}

static void _tcp_stopRetransmitTimer(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    /* we want to stop the timer. since there may be an event already scheduled,
     * lets mark our desired time as 0 so we know to cancel when the event fires. */
    tcp->retransmit.desiredTimerExpiration = 0;

    debug("%s retransmit timer disabled", tcp->super.boundString);
}

static void _tcp_setRetransmitTimeout(TCP* tcp, gint newTimeout) {
    MAGIC_ASSERT(tcp);
    tcp->retransmit.timeout = newTimeout;

    /* ensure correct range, TCP_RTO_MIN=200ms and TCP_RTO_MAX=120000ms from net/tcp.h */
    tcp->retransmit.timeout = MIN(tcp->retransmit.timeout, 120000);
    tcp->retransmit.timeout = MAX(tcp->retransmit.timeout, 200);
}

static void _tcp_updateRTTEstimate(TCP* tcp, SimulationTime timestamp) {
    MAGIC_ASSERT(tcp);

    SimulationTime now = worker_getCurrentTime();
    gint rtt = (gint)((now - timestamp) / SIMTIME_ONE_MILLISECOND);

    if(rtt <= 0) {
        rtt = 1;
    }

    /* RFC 6298 (http://tools.ietf.org/html/rfc6298) */
    if(!tcp->congestion->rttSmoothed) {
        /* first RTT measurement */
        tcp->congestion->rttSmoothed = rtt;
        tcp->congestion->rttVariance = rtt / 2;
    } else {
        /* RTTVAR = (1 - beta) * RTTVAR + beta * |SRTT - R|   (beta = 1/4) */
        tcp->congestion->rttVariance = (3 * tcp->congestion->rttVariance / 4) +
                (ABS(tcp->congestion->rttSmoothed - rtt) / 4);
        /* SRTT = (1 - alpha) * SRTT + alpha * R   (alpha = 1/8) */
        tcp->congestion->rttSmoothed = (7 * tcp->congestion->rttSmoothed / 8) + (rtt / 8);
    }

    /* RTO = SRTT + 4 * RTTVAR  (min=1s, max=60s) */
    gint newRTO = tcp->congestion->rttSmoothed + (4 * tcp->congestion->rttVariance);
    _tcp_setRetransmitTimeout(tcp, newRTO);

    debug("srtt=%d rttvar=%d rto=%d", tcp->congestion->rttSmoothed,
            tcp->congestion->rttVariance, tcp->retransmit.timeout);
}

static void _tcp_retransmitPacket(TCP* tcp, gint sequence) {
    MAGIC_ASSERT(tcp);

    Packet* packet = g_hash_table_lookup(tcp->retransmit.queue, GINT_TO_POINTER(sequence));
    /* if packet wasn't found is was most likely retransmitted from a previous SACK
     * but has yet to be received/acknowledged by the receiver */
    if(!packet) {
        return;
    }

    debug("retransmitting packet %d", sequence);

    /* remove from queue and update length and status */
    g_hash_table_steal(tcp->retransmit.queue, GINT_TO_POINTER(sequence));
    tcp->retransmit.queueLength -= packet_getPayloadLength(packet);
    packet_addDeliveryStatus(packet, PDS_SND_TCP_DEQUEUE_RETRANSMIT);

    if(_tcp_getBufferSpaceOut(tcp) > 0) {
        descriptor_adjustStatus((Descriptor*)tcp, DS_WRITABLE, TRUE);
    }

    /* reset retransmit timer and buffer packet out */
    _tcp_setRetransmitTimer(tcp, worker_getCurrentTime());
    packet_addDeliveryStatus(packet, PDS_SND_TCP_RETRANSMITTED);
    _tcp_bufferPacketOut(tcp, packet);

    tcp->info.retransmitCount++;
}


static void _tcp_flush(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    /* make sure our information is up to date */
    _tcp_updateReceiveWindow(tcp);
    _tcp_updateSendWindow(tcp);

    SimulationTime now = worker_getCurrentTime();

    /* find all packets to retransmit and add them throttled output */
    gint retransmitSequence = scoreboard_getNextRetransmit(tcp->retransmit.scoreboard);
    while(retransmitSequence != -1) {
        _tcp_retransmitPacket(tcp, retransmitSequence);
        scoreboard_markRetransmitted(tcp->retransmit.scoreboard, retransmitSequence, tcp->send.highestSequence);
        retransmitSequence = scoreboard_getNextRetransmit(tcp->retransmit.scoreboard);
    }

    /* flush packets that can now be sent to socket */
    while(!priorityqueue_isEmpty(tcp->throttledOutput)) {
        /* get the next throttled packet, in sequence order */
        Packet* packet = priorityqueue_peek(tcp->throttledOutput);

        /* break out if we have no packets left */
        if(!packet) {
            break;
        }

        guint length = packet_getPayloadLength(packet);
        PacketTCPHeader header;
        packet_getTCPHeader(packet, &header);

        if(length > 0) {
            /* we cant send it if our window is too small */
            gboolean fitsInWindow = (header.sequence < (tcp->send.unacked + tcp->send.window)) ? TRUE : FALSE;

            /* we cant send it if we dont have enough space */
            gboolean fitsInBuffer = (length <= socket_getOutputBufferSpace(&(tcp->super))) ? TRUE : FALSE;

            if(!fitsInBuffer || !fitsInWindow) {
                /* we cant send the packet yet */
                break;
            } else {
                /* we will send the data packet */
                tcp->info.lastDataSent = now;
            }
        }

        /* packet is sendable, we removed it from out buffer */
        priorityqueue_pop(tcp->throttledOutput);
        tcp->throttledOutputLength -= length;

        if(header.sequence > 0 || (header.flags & PTCP_SYN)) {
            /* store in retransmission buffer */
            _tcp_addRetransmit(tcp, packet);

            /* start retransmit timer if its not running (rfc 6298, section 5.1) */
            if(!tcp->retransmit.desiredTimerExpiration) {
                _tcp_setRetransmitTimer(tcp, now);
            }
        }

        /* update TCP header to our current advertised window and acknowledgment */
        packet_updateTCP(packet, tcp->receive.next, tcp->send.selectiveACKs, tcp->receive.window, now, tcp->receive.lastTimestamp);

        /* keep track of the last things we sent them */
        tcp->send.lastAcknowledgment = tcp->receive.next;
        tcp->send.lastWindow = tcp->receive.window;
        tcp->info.lastAckSent = now;

         /* socket will queue it ASAP */
        gboolean success = socket_addToOutputBuffer(&(tcp->super), packet);
        tcp->send.packetsSent++;
        tcp->send.highestSequence = MAX(tcp->send.highestSequence, header.sequence);

        /* we already checked for space, so this should always succeed */
        utility_assert(success);
    }

    /* any packets now in order can be pushed to our user input buffer */
    while(!priorityqueue_isEmpty(tcp->unorderedInput)) {
        Packet* packet = priorityqueue_peek(tcp->unorderedInput);

        PacketTCPHeader header;
        packet_getTCPHeader(packet, &header);

        if(header.sequence == tcp->receive.next) {
            /* move from the unordered buffer to user input buffer */
            gboolean fitInBuffer = socket_addToInputBuffer(&(tcp->super), packet);

            if(fitInBuffer) {
                priorityqueue_pop(tcp->unorderedInput);
                packet_unref(packet);
                tcp->unorderedInputLength -= packet_getPayloadLength(packet);
                (tcp->receive.next)++;
                continue;
            }
        }

        /* we could not buffer it because its out of order or we have no space */
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

static void _tcp_fastRetransmitAlert(TCP* tcp, TCPProcessFlags flags) {
    MAGIC_ASSERT(tcp);

    if(tcp->receive.state == TCPRS_OPEN) {
        // no need to exit unless frto (FRTO NOT IMPLEMENTED)
    } else if(tcp->receive.lastAcknowledgment >= tcp->receive.recoveryPoint) {
        scoreboard_clear(tcp->retransmit.scoreboard);
        if(tcp->congestion->window < tcp->congestion->threshold) {
            tcp->congestion->window = tcp->congestion->threshold;
        }
        tcp->receive.state = TCPRS_OPEN;
        return;
    }

    /* if we're not in recovery state and data was lost, enter into fast recovery */
    if(tcp->receive.state != TCPRS_RECOVERY && (flags & TCP_PF_DATA_LOST)) {
        tcp->receive.state = TCPRS_RECOVERY;
        tcp->receive.recoveryPoint = tcp->send.highestSequence;

        tcp->congestion->threshold = tcpCongestion_packetLoss(tcp->congestion);
        tcp->congestion->window = tcp->congestion->threshold;
    }
}

void tcp_retransmitTimerExpired(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    /* a timer expired, update our timer tracking state */
    SimulationTime now = worker_getCurrentTime();
    SimulationTime* scheduledTimerExpirationPtr = priorityqueue_pop(tcp->retransmit.scheduledTimerExpirations);
    utility_assert(scheduledTimerExpirationPtr);
    g_free(scheduledTimerExpirationPtr);

    debug("%s a scheduled retransmit timer expired", tcp->super.boundString);

    /* if we are closed, we don't care */
    if(tcp->state == TCPS_CLOSED) {
        _tcp_stopRetransmitTimer(tcp);
        _tcp_clearRetransmit(tcp, (guint)-1);
        return;
    }

    if(g_hash_table_size(tcp->retransmit.queue) == 0) {
        _tcp_stopRetransmitTimer(tcp);
        return;
    }

    /* if the timer should be off or was reset, ignore this event */
    if(tcp->retransmit.desiredTimerExpiration == 0) {
        return;
    } else if(tcp->retransmit.desiredTimerExpiration > now) {
        /* the timer was reset after this event was scheduled, check if we need to
         * schedule another event, or if we can do it when the next event fires instead */
        _tcp_scheduleRetransmitTimerIfNeeded(tcp, now);
        return;
    }

    /* rfc 6298, section 5.4-5.7 (http://tools.ietf.org/html/rfc6298)
     * if we get here, this is a valid timer expiration and we need to do a retransmission
     * do exponential backoff */
    tcp->retransmit.backoffCount++;
    _tcp_setRetransmitTimeout(tcp, tcp->retransmit.timeout * 2);
    _tcp_setRetransmitTimer(tcp, now);

    /* update the scoreboard by marking this as lost */
    scoreboard_markLoss(tcp->retransmit.scoreboard, tcp->receive.lastAcknowledgment, tcp->send.highestSequence);

    debug("[CONG-LOSS] cwnd=%d ssthresh=%d rtt=%d sndbufsize=%d sndbuflen=%d rcvbufsize=%d rcbuflen=%d retrans=%d ploss=%f",
            tcp->congestion->window, tcp->congestion->threshold, tcp->congestion->rttSmoothed, 
            tcp->super.outputBufferLength, tcp->super.outputBufferSize, tcp->super.inputBufferLength, tcp->super.inputBufferSize, 
            tcp->info.retransmitCount, (float)tcp->info.retransmitCount / tcp->send.packetsSent);

    tcp->congestion->state = TCP_CCS_AVOIDANCE;

    /* resend the next unacked packet */
    gint sequence = tcp->send.unacked;
    if(tcp->send.unacked == 1 && g_hash_table_lookup(tcp->retransmit.queue, GINT_TO_POINTER(0))) {
        sequence = 0;
    }

    debug("%s valid timer expiration (congestion event) occurred on packet %d", tcp->super.boundString, sequence);

    _tcp_retransmitPacket(tcp, sequence);
    _tcp_flush(tcp);
}

gboolean tcp_isFamilySupported(TCP* tcp, sa_family_t family) {
    MAGIC_ASSERT(tcp);
    return family == AF_INET || family == AF_UNIX ? TRUE : FALSE;
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
//  tcpinfo->tcpi_ca_state;
//  tcpinfo->tcpi_retransmits;
//  tcpinfo->tcpi_probes;
//  tcpinfo->tcpi_backoff;
//  tcpinfo->tcpi_options;
//  tcpinfo->tcpi_snd_wscale;
//  tcpinfo->tcpi_rcv_wscale;

//  tcpinfo->tcpi_rto;
//  tcpinfo->tcpi_ato;
    tcpinfo->tcpi_snd_mss = (u_int32_t)(CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH);
    tcpinfo->tcpi_rcv_mss = (u_int32_t)(CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH);

    tcpinfo->tcpi_unacked = tcp->send.next - tcp->send.unacked;
//  tcpinfo->tcpi_sacked;
//  tcpinfo->tcpi_lost;
    tcpinfo->tcpi_retrans = (u_int32_t) tcp->info.retransmitCount;
//  tcpinfo->tcpi_fackets;

    /* Times. */
    tcpinfo->tcpi_last_data_sent = (u_int32_t)(tcp->info.lastDataSent/SIMTIME_ONE_MICROSECOND);
    tcpinfo->tcpi_last_ack_sent = (u_int32_t)(tcp->info.lastAckSent/SIMTIME_ONE_MICROSECOND);
    tcpinfo->tcpi_last_data_recv = (u_int32_t)(tcp->info.lastDataReceived/SIMTIME_ONE_MICROSECOND);
    tcpinfo->tcpi_last_ack_recv = (u_int32_t)(tcp->info.lastAckReceived/SIMTIME_ONE_MICROSECOND);

    /* Metrics. */
    tcpinfo->tcpi_pmtu = (u_int32_t)(CONFIG_MTU);
//  tcpinfo->tcpi_rcv_ssthresh;
    tcpinfo->tcpi_rtt = tcp->congestion->rttSmoothed;
    tcpinfo->tcpi_rttvar = tcp->congestion->rttVariance;
    tcpinfo->tcpi_snd_ssthresh = tcp->congestion->threshold;
    tcpinfo->tcpi_snd_cwnd = tcp->congestion->window;
    tcpinfo->tcpi_advmss = (u_int32_t)(CONFIG_MTU - CONFIG_HEADER_SIZE_TCPIPETH);
//  tcpinfo->tcpi_reordering;

    tcpinfo->tcpi_rcv_rtt = tcp->info.rtt;
    tcpinfo->tcpi_rcv_space = tcp->receive.lastWindow;

    tcpinfo->tcpi_total_retrans = tcp->info.retransmitCount;
}


gint tcp_connectToPeer(TCP* tcp, in_addr_t ip, in_port_t port, sa_family_t family) {
    MAGIC_ASSERT(tcp);

    gint error = tcp_getConnectError(tcp);
    if(error == EISCONN && !(tcp->flags & TCPF_CONNECT_SIGNALED)) {
        /* we need to signal that connect was successful  */
        tcp->flags |= TCPF_CONNECT_SIGNALED;
        return 0;
    } else if(error) {
        return error;
    }

    /* no error, so we need to do the connect */

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
        /* listen sockets should have no data, and should not be readable if no pending conns */
        utility_assert(socket_getInputBufferLength(&tcp->super) == 0);
        descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, FALSE);
        return EWOULDBLOCK;
    }

    /* double check the pending child before its accepted */
    TCP* tcpChild = g_queue_pop_head(tcp->server->pending);
    if(!tcpChild) {
        return ECONNABORTED;
    }

    MAGIC_ASSERT(tcpChild);
    if(tcpChild->error == TCPE_CONNECTION_RESET) {
        return ECONNABORTED;
    }

    /* better have a peer if we are established */
    utility_assert(tcpChild->super.peerIP && tcpChild->super.peerPort);

    /* child now gets "accepted" */
    MAGIC_ASSERT(tcpChild->child);
    tcpChild->child->state = TCPCS_ACCEPTED;

    /* update child descriptor status */
    descriptor_adjustStatus(&(tcpChild->super.super.super), DS_ACTIVE|DS_WRITABLE, TRUE);

    /* update server descriptor status */
    if(g_queue_get_length(tcp->server->pending) > 0) {
        descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, TRUE);
    } else {
        descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, FALSE);
    }

    *acceptedHandle = tcpChild->super.super.super.handle;
    if(ip) {
        *ip = tcpChild->super.peerIP;
    }
    if(port) {
        *port = tcpChild->super.peerPort;
    }

    Tracker* tracker = host_getTracker(worker_getCurrentHost());
    tracker_updateSocketPeer(tracker, *acceptedHandle, *ip, ntohs(tcpChild->super.peerPort));

    return 0;
}

static TCP* _tcp_getSourceTCP(TCP* tcp, in_addr_t ip, in_port_t port) {
    MAGIC_ASSERT(tcp);

    /* servers may have children keyed by ip:port */
    if(tcp->server) {
        MAGIC_ASSERT(tcp->server);

        /* children are multiplexed based on remote ip and port */
        guint childKey = utility_ipPortHash(ip, port);
        TCP* tcpChild = g_hash_table_lookup(tcp->server->children, &childKey);

        if(tcpChild) {
            return tcpChild;
        }
    }

    return tcp;
}

static GList* _tcp_removeSacks(GList* selectiveACKs, gint sequence) {
    GList *unacked = NULL;
    if(selectiveACKs) {
        GList *iter = selectiveACKs;
        while(iter) {
            gint sackSequence = GPOINTER_TO_INT(iter->data);

            if(sackSequence > sequence) {
                unacked = g_list_append(unacked, iter->data);
            }

            iter = g_list_next(iter);
        }
        g_list_free(selectiveACKs);
    }
    return unacked;
}

TCPProcessFlags _tcp_dataProcessing(TCP* tcp, Packet* packet, PacketTCPHeader *header) {
    MAGIC_ASSERT(tcp);

    TCPProcessFlags flags = TCP_PF_NONE;
    SimulationTime now = worker_getCurrentTime();
    guint packetLength = packet_getPayloadLength(packet);

    /* it has data, check if its in the correct range */
    if(header->sequence >= (tcp->receive.next + tcp->receive.window)) {
        /* its too far ahead to accept now, but they should re-send it */
        flags |= TCP_PF_PROCESSED;
        packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
    } else if(header->sequence >= tcp->receive.next) {
        /* its in our window, so we can accept the data */
        flags |= TCP_PF_PROCESSED;

        /*
         * if this is THE next packet, we MUST accept it to avoid
         * deadlocks (unless we are blocked b/c user should read)
         */
        gboolean isNextPacket = (header->sequence == tcp->receive.next) ? TRUE : FALSE;
        gboolean packetFits = (packetLength <= _tcp_getBufferSpaceIn(tcp)) ? TRUE : FALSE;

        /* SACK: if not next packet, one was dropped and we need to include this in the selective ACKs */
        if(!isNextPacket) {
            tcp->send.selectiveACKs = g_list_append(tcp->send.selectiveACKs, GINT_TO_POINTER(header->sequence));
        } else if(tcp->send.selectiveACKs && g_list_length(tcp->send.selectiveACKs) > 0) {
            /* find the first gap in SACKs and remove everything before it */
            GList *iter = g_list_first(tcp->send.selectiveACKs);
            GList *next = g_list_next(iter);

            gint firstSequence = GPOINTER_TO_INT(iter->data);
            if(firstSequence <= header->sequence + 1) {
                while(next) {
                    gint currSequence = GPOINTER_TO_INT(iter->data);
                    gint nextSequence = GPOINTER_TO_INT(next->data);
                    /* check for a gap in sequences */
                    if(currSequence + 1 < nextSequence && currSequence > header->sequence) {
                        break;
                    }
                    iter = next;
                    next = g_list_next(iter);
                }

                tcp->send.selectiveACKs = _tcp_removeSacks(tcp->send.selectiveACKs, GPOINTER_TO_INT(iter->data));
            }
        }

        DescriptorStatus s = descriptor_getStatus((Descriptor*) tcp);
        gboolean waitingUserRead = (s & DS_READABLE) ? TRUE : FALSE;
        
        if((isNextPacket && !waitingUserRead) || (packetFits)) {
            /* make sure its in order */
            _tcp_bufferPacketIn(tcp, packet);
            tcp->info.lastDataReceived = now;
        } else {
            debug("no space for packet even though its in our window");
            packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
        }
    }

    return flags;
}

TCPProcessFlags _tcp_ackProcessing(TCP* tcp, Packet* packet, PacketTCPHeader *header, gint* nPacketsAcked) {
    MAGIC_ASSERT(tcp);

    TCPProcessFlags flags = TCP_PF_PROCESSED;
    SimulationTime now = worker_getCurrentTime();

    guint32 prevSeq = tcp->receive.lastSequence;
    guint32 prevAck = tcp->receive.lastAcknowledgment;
    guint32 prevWin = tcp->receive.lastWindow;

    /* the ack is in our send window */
    gboolean isValidAck = (header->acknowledgment > tcp->send.unacked) &&
            (header->acknowledgment <= tcp->send.next);
    /* same ack and window opened, or new ack and window changed */
    gboolean isValidWindow = ((header->acknowledgment == tcp->receive.lastAcknowledgment) &&
            (header->window > prevWin)) || ((header->acknowledgment > tcp->receive.lastAcknowledgment) &&
                    (header->window != prevWin));

    *nPacketsAcked = 0;
    if(isValidAck) {
        /* update their advertisements */
        tcp->receive.lastAcknowledgment = (guint32) header->acknowledgment;

        /* some data we sent got acknowledged */
        *nPacketsAcked = header->acknowledgment - tcp->send.unacked;
        tcp->send.unacked = header->acknowledgment;

        if(*nPacketsAcked > 0) {
            flags |= TCP_PF_DATA_ACKED;

            /* increase send buffer size with autotuning */
            if(tcp->autotune.isEnabled && host_autotuneSendBuffer(worker_getCurrentHost())) {
                _tcp_autotuneSendBuffer(tcp);
            }
        }

        /* the packets just acked are 'released' from retransmit queue */
        _tcp_clearRetransmit(tcp, header->acknowledgment);

        /* if we had congestion, reset our state (rfc 6298, section 5) */
        if(tcp->retransmit.backoffCount > 2) {
            tcp->congestion->rttSmoothed = 0;
            tcp->congestion->rttVariance = 0;
            _tcp_setRetransmitTimeout(tcp, 1);
        }
        tcp->retransmit.backoffCount = 0;
    }

    if(isValidWindow) {
        /* accept the window update */
        tcp->receive.lastWindow = (guint32) header->window;
    }

    /* update retransmit state (rfc 6298, section 5.2-5.3) */
    if(tcp->retransmit.queueLength == 0) {
        /* all outstanding data has been acked */
        _tcp_stopRetransmitTimer(tcp);
    } else if(*nPacketsAcked > 0) {
        /* new data has been acked */
        _tcp_setRetransmitTimer(tcp, now);
    }

    tcp->info.lastAckReceived = now;

    return flags;
}

static void _tcp_logCongestionInfo(TCP* tcp) {
    gsize outSize = socket_getOutputBufferSize(&tcp->super);
    gsize outLength = socket_getOutputBufferLength(&tcp->super);
    gsize inSize = socket_getInputBufferSize(&tcp->super);
    gsize inLength = socket_getInputBufferLength(&tcp->super);
    double ploss = (double) (tcp->info.retransmitCount / tcp->send.packetsSent);

    debug("[CONG-AVOID] cwnd=%d ssthresh=%d rtt=%d "
            "sndbufsize=%"G_GSIZE_FORMAT" sndbuflen=%"G_GSIZE_FORMAT" rcvbufsize=%"G_GSIZE_FORMAT" rcbuflen=%"G_GSIZE_FORMAT" "
            "retrans=%"G_GSIZE_FORMAT" ploss=%f",
            tcp->congestion->window, tcp->congestion->threshold, tcp->congestion->rttSmoothed,
            outSize, outLength, inSize, inLength, tcp->info.retransmitCount, ploss);
}

/* return TRUE if the packet should be retransmitted */
void tcp_processPacket(TCP* tcp, Packet* packet) {
    MAGIC_ASSERT(tcp);

    /* fetch the TCP info from the packet */
    PacketTCPHeader header;
    packet_getTCPHeader(packet, &header);
    guint packetLength = packet_getPayloadLength(packet);

    /* if we run a server, the packet could be for an existing child */
    tcp = _tcp_getSourceTCP(tcp, header.sourceIP, header.sourcePort);

    /* now we have the true TCP for the packet */
    MAGIC_ASSERT(tcp);

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
        return;
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
    TCPProcessFlags flags = TCP_PF_NONE;
    enum ProtocolTCPFlags responseFlags = PTCP_NONE;

    switch(tcp->state) {
        case TCPS_LISTEN: {
            /* receive SYN, send SYNACK, move to SYNRECEIVED */
            if(header.flags & PTCP_SYN) {
                MAGIC_ASSERT(tcp->server);
                flags |= TCP_PF_PROCESSED;

                /* we need to multiplex a new child */
                Host* node = worker_getCurrentHost();
                gint multiplexedHandle = host_createDescriptor(node, DT_TCPSOCKET);
                TCP* multiplexed = (TCP*) host_lookupDescriptor(node, multiplexedHandle);

                multiplexed->child = _tcpchild_new(multiplexed, tcp, header.sourceIP, header.sourcePort);
                utility_assert(g_hash_table_lookup(tcp->server->children, &(multiplexed->child->key)) == NULL);
                descriptor_ref(multiplexed);
                g_hash_table_replace(tcp->server->children, &(multiplexed->child->key), multiplexed);

                multiplexed->receive.start = header.sequence;
                multiplexed->receive.next = multiplexed->receive.start + 1;

                debug("%s <-> %s: server multiplexed child socket %s <-> %s",
                        tcp->super.boundString, tcp->super.peerString,
                        multiplexed->super.boundString, multiplexed->super.peerString);

                _tcp_setState(multiplexed, TCPS_SYNRECEIVED);

                /* child will send response */
                tcp = multiplexed;
                responseFlags = PTCP_SYN|PTCP_ACK;
            }
            break;
        }

        case TCPS_SYNSENT: {
            /* receive SYNACK, send ACK, move to ESTABLISHED */
            if((header.flags & PTCP_SYN) && (header.flags & PTCP_ACK)) {
                flags |= TCP_PF_PROCESSED;
                tcp->receive.start = header.sequence;
                tcp->receive.next = tcp->receive.start + 1;

                responseFlags |= PTCP_ACK;
                _tcp_setState(tcp, TCPS_ESTABLISHED);

                /* remove the SYN from the retransmit queue */
                _tcp_clearRetransmit(tcp, 1);
            }
            /* receive SYN, send ACK, move to SYNRECEIVED (simultaneous open) */
            else if(header.flags & PTCP_SYN) {
                flags |= TCP_PF_PROCESSED;
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
                flags |= TCP_PF_PROCESSED;
                _tcp_setState(tcp, TCPS_ESTABLISHED);

                /* remove the SYNACK from the retransmit queue */
                _tcp_clearRetransmit(tcp, 1);

                /* if this is a child, mark it accordingly */
                if(tcp->child) {
                    tcp->child->state = TCPCS_PENDING;
                    g_queue_push_tail(tcp->child->parent->server->pending, tcp);
                    /* user should accept new child from parent */
                    descriptor_adjustStatus(&(tcp->child->parent->super.super.super), DS_READABLE, TRUE);
                }
            }
            break;
        }

        case TCPS_ESTABLISHED: {
            /* receive FIN, send FINACK, move to CLOSEWAIT */
            if(header.flags & PTCP_FIN) {
                flags |= TCP_PF_PROCESSED;

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
                flags |= TCP_PF_PROCESSED;
                _tcp_setState(tcp, TCPS_FINWAIT2);
            }
            /* receive FIN, send FINACK, move to CLOSING (simultaneous close) */
            else if(header.flags & PTCP_FIN) {
                flags |= TCP_PF_PROCESSED;
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
                flags |= TCP_PF_PROCESSED;
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
                flags |= TCP_PF_PROCESSED;
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
                flags |= TCP_PF_PROCESSED;
                _tcp_setState(tcp, TCPS_CLOSED);
                /* we closed, cant use tcp anymore */
                return;
            }
            break;
        }

        default:
        case TCPS_CLOSED: {
            /* stray packet, drop without retransmit */
            packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
            return;
            break;
        }
    }

    /* listening sockets are not connected and do not exchange data */
    if(tcp->state == TCPS_LISTEN) {
        if(!(flags & TCP_PF_PROCESSED)) {
            packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
        }
        return;
    }

    SimulationTime now = worker_getCurrentTime();
    gint nPacketsAcked = 0;

    if(packetLength > 0) {
        flags |= _tcp_dataProcessing(tcp, packet, &header);
    }

    if(header.flags & PTCP_ACK) {
        flags |= _tcp_ackProcessing(tcp, packet, &header, &nPacketsAcked);
    }

    /* if it is a spurious packet, drop it */
    if(!(flags & TCP_PF_PROCESSED)) {
        utility_assert(responseFlags == PTCP_NONE);
        packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
        return;
    }

    /* update the scoreboard and see if any packets have been lost */
    GList* selectiveACKs = packet_copyTCPSelectiveACKs(packet);
    flags |= scoreboard_update(tcp->retransmit.scoreboard, selectiveACKs, tcp->send.unacked, tcp->send.next);
    if(selectiveACKs) {
        g_list_free(selectiveACKs);
    }

    /* update the last time stamp value (RFC 1323) */
    tcp->receive.lastTimestamp = header.timestampValue;
    if(header.timestampEcho && tcp->retransmit.backoffCount == 0) {
        _tcp_updateRTTEstimate(tcp, header.timestampEcho);
    }

    gboolean isAckDubious = (!(flags & TCP_PF_DATA_ACKED) || (flags & TCP_PF_DATA_SACKED));
    gboolean mayRaiseWindow = (tcp->receive.state != TCPRS_RECOVERY);

    if(isAckDubious) {
        if((flags & TCP_PF_DATA_ACKED) && mayRaiseWindow) {
            tcpCongestion_avoidance(tcp->congestion, tcp->send.next, nPacketsAcked, tcp->send.unacked);
            _tcp_logCongestionInfo(tcp);
        }

        _tcp_fastRetransmitAlert(tcp, flags);
    } else if(flags & TCP_PF_DATA_ACKED) {
        tcpCongestion_avoidance(tcp->congestion, tcp->send.next, nPacketsAcked, tcp->send.unacked);
        _tcp_logCongestionInfo(tcp);
    }

    /* now flush as many packets as we can to socket */
    _tcp_flush(tcp);

    /* send ack if they need updates but we didn't send any yet (selective acks) */
    if((tcp->receive.next > tcp->send.lastAcknowledgment) ||
        (tcp->receive.window != tcp->send.lastWindow) ||
        (tcp->congestion->fastRetransmit && header.sequence > tcp->receive.next)) 
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

    /* clear it so we dont send outdated timestamp echos */
    tcp->receive.lastTimestamp = 0;
}

void tcp_dropPacket(TCP* tcp, Packet* packet) {
    MAGIC_ASSERT(tcp);

    /* fetch the TCP info from the packet */
    PacketTCPHeader header;
    packet_getTCPHeader(packet, &header);

    /* if we run a server, the packet could be for an existing child */
    tcp = _tcp_getSourceTCP(tcp, header.destinationIP, header.destinationPort);

    /* now we have the true TCP for the packet */
    MAGIC_ASSERT(tcp);

    debug("dropped packet %d", header.sequence);

    //scoreboard_markLoss(tcp->retransmit.scoreboard, header.sequence, tcp->send.highestSequence);
    scoreboard_packetDropped(tcp->retransmit.scoreboard, header.sequence);
    //_tcp_retransmitPacket(tcp, header.sequence);
    
    _tcp_flush(tcp);

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

static void _tcp_sendWindowUpdate(TCP* tcp, gpointer data) {
    MAGIC_ASSERT(tcp);
    debug("%s <-> %s: receive window opened, advertising the new "
            "receive window %"G_GUINT32_FORMAT" as an ACK control packet",
            tcp->super.boundString, tcp->super.peerString, tcp->receive.window);

    // XXX we may be in trouble if this packet gets dropped
    Packet* windowUpdate = _tcp_createPacket(tcp, PTCP_ACK, NULL, 0);
    _tcp_bufferPacketOut(tcp, windowUpdate);
    _tcp_flush(tcp);

    tcp->receive.windowUpdatePending = FALSE;
    descriptor_unref(&tcp->super.super.super);
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
            packet_addDeliveryStatus(tcp->partialUserDataPacket, PDS_RCV_SOCKET_DELIVERED);
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
        packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DELIVERED);
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

    /* update the receive buffer size based on new packets received */
    if(tcp->autotune.isEnabled) {
        Host* host = worker_getCurrentHost();
        if(host_autotuneReceiveBuffer(host)) {
            _tcp_autotuneReceiveBuffer(tcp, totalCopied);
        }
    }

    /* if we have advertised a 0 window because the application wasn't reading,
     * we now have to update the window and let the sender know */
    _tcp_updateReceiveWindow(tcp);
    if(tcp->receive.window > tcp->send.lastWindow && !tcp->receive.windowUpdatePending) {
        /* our receive window just opened, make sure the sender knows it can
         * send more. otherwise we get into a deadlock situation!
         * make sure we don't send multiple events when read is called many times per instant */
        descriptor_ref(&tcp->super.super.super);
        CallbackEvent* event = callback_new((CallbackFunc)_tcp_sendWindowUpdate, tcp, NULL);
        worker_scheduleEvent((Event*)event, (SimulationTime)1, 0);
        tcp->receive.windowUpdatePending = TRUE;
    }

    debug("%s <-> %s: receiving %"G_GSIZE_FORMAT" user bytes", tcp->super.boundString, tcp->super.peerString, totalCopied);

    return (gssize) (totalCopied == 0 ? -1 : totalCopied);
}

void tcp_free(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    priorityqueue_free(tcp->throttledOutput);
    priorityqueue_free(tcp->unorderedInput);
    g_hash_table_destroy(tcp->retransmit.queue);
    priorityqueue_free(tcp->retransmit.scheduledTimerExpirations);

    if(tcp->child) {
        MAGIC_ASSERT(tcp->child);
        MAGIC_ASSERT(tcp->child->parent);
        MAGIC_ASSERT(tcp->child->parent->server);

        /* remove parents reference to child, if it exists */
        if(tcp->child->parent->server->children) {
            g_hash_table_remove(tcp->child->parent->server->children, &(tcp->child->key));
        }

        _tcpchild_free(tcp->child);
    }

    if(tcp->server) {
        _tcpserver_free(tcp->server);
    }

    tcpCongestion_free(tcp->congestion);
    scoreboard_free(tcp->retransmit.scoreboard);

    MAGIC_CLEAR(tcp);
    g_free(tcp);
}

void tcp_close(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    debug("%s <-> %s:  user closed connection", tcp->super.boundString, tcp->super.peerString);
    tcp->flags |= TCPF_LOCAL_CLOSED;

    switch (tcp->state) {
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
            /* dont send a FIN
             * but make sure we set state to closed so we unbind the socket */
            _tcp_setState(tcp, TCPS_CLOSED);
            return;
        }
    }

    /* send a FIN */
    Packet* packet = _tcp_createPacket(tcp, PTCP_FIN, NULL, 0);

    /* dont have to worry about space since this has no payload */
    _tcp_bufferPacketOut(tcp, packet);
    _tcp_flush(tcp);

    /* the user closed the connection, so should never interact with the socket again */
    descriptor_adjustStatus((Descriptor*)tcp, DS_ACTIVE, FALSE);
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
    (SocketIsFamilySupportedFunc) tcp_isFamilySupported,
    (SocketConnectToPeerFunc) tcp_connectToPeer,
    (SocketDropFunc) tcp_dropPacket,
    MAGIC_VALUE
};

TCP* tcp_new(gint handle, guint receiveBufferSize, guint sendBufferSize) {
    TCP* tcp = g_new0(TCP, 1);
    MAGIC_INIT(tcp);

    socket_init(&(tcp->super), &tcp_functions, DT_TCPSOCKET, handle, receiveBufferSize, sendBufferSize);

    Configuration* config = worker_getConfig();
    guint32 initial_window = config->initialTCPWindow;

    TCPCongestionType congestionType = tcpCongestion_getType(config->tcpCongestionControl);
    if(congestionType == TCP_CC_UNKNOWN) {
        warning("unable to find congestion control algorithm '%s', defaulting to CUBIC", config->tcpCongestionControl);
        congestionType = TCP_CC_CUBIC;
    }

    switch(congestionType) {
        case TCP_CC_AIMD:
            tcp->congestion = (TCPCongestion*)aimd_new(initial_window, config->tcpSlowStartThreshold);
            break;

        case TCP_CC_RENO:
            tcp->congestion = (TCPCongestion*)reno_new(initial_window, config->tcpSlowStartThreshold);
            break;

        case TCP_CC_CUBIC:
            tcp->congestion = (TCPCongestion*)cubic_new(initial_window, config->tcpSlowStartThreshold);
            break;

        case TCP_CC_UNKNOWN:
        default:
            error("Failed to initialize TCP congestion control for %s", config->tcpCongestionControl);
            break;
    }

    tcp->send.window = initial_window;
    tcp->send.lastWindow = initial_window;
    tcp->receive.window = initial_window;
    tcp->receive.lastWindow = initial_window;

    /* 0 is saved for representing control packets */
    guint32 initialSequenceNumber = 1;

    tcp->send.unacked = initialSequenceNumber;
    tcp->send.next = initialSequenceNumber;
    tcp->send.end = initialSequenceNumber;
    tcp->send.lastAcknowledgment = initialSequenceNumber;
    tcp->receive.end = initialSequenceNumber;
    tcp->receive.next = initialSequenceNumber;
    tcp->receive.start = initialSequenceNumber;
    tcp->receive.lastAcknowledgment = initialSequenceNumber;

    tcp->autotune.isEnabled = TRUE;

    tcp->throttledOutput =
            priorityqueue_new((GCompareDataFunc)packet_compareTCPSequence, NULL, (GDestroyNotify)packet_unref);
    tcp->unorderedInput =
            priorityqueue_new((GCompareDataFunc)packet_compareTCPSequence, NULL, (GDestroyNotify)packet_unref);
    tcp->retransmit.queue =
            g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)packet_unref);
    tcp->retransmit.scoreboard = scoreboard_new();
    tcp->retransmit.scheduledTimerExpirations =
            priorityqueue_new((GCompareDataFunc)utility_simulationTimeCompare, NULL, g_free);

    /* TCP_TIMEOUT_INIT=1000ms from net/tcp.h */
    _tcp_setRetransmitTimeout(tcp, 1000);

    return tcp;
}
