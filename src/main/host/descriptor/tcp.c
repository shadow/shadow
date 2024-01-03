/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/tcp.h"

#include <errno.h>
#include <math.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/core/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp_cong.h"
#include "main/host/descriptor/tcp_cong_reno.h"
#include "main/host/descriptor/tcp_retransmit_tally.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/packet.h"
#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"

enum TCPState {
    TCPS_CLOSED, TCPS_LISTEN,
    TCPS_SYNSENT, TCPS_SYNRECEIVED, TCPS_ESTABLISHED,
    TCPS_FINWAIT1, TCPS_FINWAIT2, TCPS_CLOSING, TCPS_TIMEWAIT,
    TCPS_CLOSEWAIT, TCPS_LASTACK,
};

static const gchar* _tcpStateStrings[] = {
    "TCPS_CLOSED", "TCPS_LISTEN",
    "TCPS_SYNSENT", "TCPS_SYNRECEIVED", "TCPS_ESTABLISHED",
    "TCPS_FINWAIT1", "TCPS_FINWAIT2", "TCPS_CLOSING", "TCPS_TIMEWAIT",
    "TCPS_CLOSEWAIT", "TCPS_LASTACK"
};

static const gchar* _tcp_stateToAscii(enum TCPState state) {
    return _tcpStateStrings[state];
}

enum TCPFlags {
    TCPF_NONE = 0,
    TCPF_LOCAL_CLOSED_RD = 1 << 0,
    TCPF_LOCAL_CLOSED_WR = 1 << 1,
    TCPF_REMOTE_CLOSED = 1 << 2,
    TCPF_EOF_RD_SIGNALED = 1 << 3,
    TCPF_EOF_WR_SIGNALED = 1 << 4,
    TCPF_RESET_SIGNALED = 1 << 5,
    TCPF_WAS_ESTABLISHED = 1 << 6,
    TCPF_CONNECT_SIGNAL_NEEDED = 1 << 7,
    TCPF_SHOULD_SEND_WR_FIN = 1 << 8,
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
    /* the handle to return when the socket is accepted */
    int handle;
    MAGIC_DECLARE;
};

typedef struct _TCPServer TCPServer;
struct _TCPServer {
    /* children will be registered in this process' descriptor table */
    pid_t processForChildren;
    /* all children of this server */
    GHashTable* children;
    /* pending children to accept in order. */
    GQueue *pending;
    /* maximum number of pending connections (capped at SHADOW_SOMAXCONN) */
    guint pendingMax;
    guint pendingCount;
    /* IP and port of the last peer trying to connect to us; both in network byte order */
    in_addr_t lastPeerIP;
    in_port_t lastPeerPort;
    /* last interface IP we received on in network byte order */
    in_addr_t lastIP;
    MAGIC_DECLARE;
};

static void _tcp_logCongestionInfo(TCP* tcp);

struct _TCP {
    LegacySocket super;

    // this adds a circular reference between the rust `LegacyTcpSocket` and this `TCP`, but we
    // can't avoid it because `_flush` calls back into the host
    InetSocketWeak* rustSocket;

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
        CSimulationTime lastTimestamp;
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
        /* total number of quick acknowledgments sent */
        guint32 numQuickACKsSent;
        gboolean delayedACKIsScheduled;
        guint32 delayedACKCounter;
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
        CSimulationTime desiredTimerExpiration;
        /* number of times we backed off due to congestion */
        guint backoffCount;

        void *tally;
    } retransmit;

    /* tcp autotuning for the send and recv buffers */
    struct {
        gboolean isEnabled;
        gboolean didInitializeBufferSizes;
        gboolean userDisabledSend;
        gboolean userDisabledReceive;
        gsize bytesCopied;
        CEmulatedTime lastAdjustment;
        gsize space;
    } autotune;

    /* congestion object for implementing different types of congestion control (aimd, reno, cubic) */
    TCPCong cong;

    struct {
      gint rttSmoothed;
      gint rttVariance;
    } timing;

    /* TODO: these should probably be stamped when the network interface sends
     * instead of when the tcp layer sends down to the socket layer */
    struct {
        CSimulationTime lastDataSent;
        CSimulationTime lastAckSent;
        CSimulationTime lastDataReceived;
        CSimulationTime lastAckReceived;
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

static void _rswlog(const TCP *tcp, const char *format, ...) {
#ifdef RSWLOG
    SimulationTime now = worker_getCurrentTime();
    double dtime = (double)(now) / (1.0E9);

    fprintf(stderr, "@%fs (%s %s)\t", dtime, tcp->super.boundString, tcp->super.peerString);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
#endif // RSWLOG
}

static guint _ipPortHash(in_addr_t ip, in_port_t port) {
    GString* buffer = g_string_new(NULL);
    g_string_printf(buffer, "%u:%u", ip, port);
    guint hash_value = g_str_hash(buffer->str);
    g_string_free(buffer, TRUE);
    return hash_value;
}

static gint _simulationTimeCompare(const CSimulationTime* value1, const CSimulationTime* value2,
                                   gpointer userData) {
    utility_debugAssert(value1 && value2);
    /* return neg if first before second, pos if second before first, 0 if equal */
    return (*value1) == (*value2) ? 0 : (*value1) < (*value2) ? -1 : +1;
}

static void _tcp_flush(TCP* tcp, const Host* host);

static TCP* _tcp_fromLegacyFile(LegacyFile* descriptor) {
    utility_debugAssert(legacyfile_getType(descriptor) == DT_TCPSOCKET);
    return (TCP*)descriptor;
}

/* Address and port must be in network byte order. */
static TCPChild* _tcpchild_new(TCP* tcp, TCP* parent, int handle, in_addr_t peerIP,
                               in_port_t peerPort) {
    MAGIC_ASSERT(tcp);
    MAGIC_ASSERT(parent);

    TCPChild* child = g_new0(TCPChild, 1);
    MAGIC_INIT(child);

    /* my parent can find me by my key */
    child->key = _ipPortHash(peerIP, peerPort);

    legacyfile_ref(parent);
    child->parent = parent;

    child->state = TCPCS_INCOMPLETE;
    legacysocket_setPeerName(&(tcp->super), peerIP, peerPort);

    child->handle = handle;

    /* the child is bound to the parent server's address, because all packets
     * coming from the child should appear to be coming from the server itself */
    in_addr_t parentAddress;
    in_port_t parentPort;
    legacysocket_getSocketName(&(parent->super), &parentAddress, &parentPort);
    legacysocket_setSocketName(&(tcp->super), parentAddress, parentPort);

    /* we have the same name and peer as the parent, but we do not associate
     * on the interface. the parent will receive packets and multiplex to us. */

    return child;
}

static void _tcpchild_free(TCPChild* child) {
    MAGIC_ASSERT(child);
    MAGIC_ASSERT(child->parent);
    MAGIC_ASSERT(child->parent->server);

    /* remove parents reference to child, if it exists */
    if (child->parent->server->children) {
        g_hash_table_remove(child->parent->server->children, &(child->key));
    }

    legacyfile_unref(child->parent);

    MAGIC_CLEAR(child);
    g_free(child);
}

static void _tcpserver_updateBacklog(TCPServer* server, gint _backlog) {
    MAGIC_ASSERT(server);

    // linux also makes this cast, so negative backlogs wrap around to large positive backlogs
    // https://elixir.free-electrons.com/linux/v5.11.22/source/net/ipv4/af_inet.c#L212
    guint backlog = _backlog;

    // the linux '__sys_listen()' applies the somaxconn max to all protocols
    backlog = MIN(backlog, SHADOW_SOMAXCONN);

    // linux uses a limit of one greater than the provided backlog (ex: a backlog value of 0 allows
    // for one incoming connection at a time)
    if (backlog < G_MAXUINT) {
        backlog += 1;
    }

    server->pendingMax = backlog;
}

static TCPServer* _tcpserver_new(gint backlog, pid_t processForChildren) {
    TCPServer* server = g_new0(TCPServer, 1);
    MAGIC_INIT(server);

    // store weak references to children
    server->children =
        g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)legacyfile_unrefWeak);
    server->pending = g_queue_new();
    server->pendingMax = 0;

    server->processForChildren = processForChildren;

    _tcpserver_updateBacklog(server, backlog);

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

static bool _tcpserver_acceptQueueFull(TCPServer* server) {
    MAGIC_ASSERT(server);
    return server->pendingCount >= server->pendingMax;
}

struct TCPCong_ *tcp_cong(TCP *tcp) {
    return &tcp->cong;
}

void tcp_clearAllChildrenIfServer(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    if(tcp->server && tcp->server->children) {
        g_hash_table_destroy(tcp->server->children);
        tcp->server->children = NULL;
    }
}

/* Returned address is in network byte order. */
static in_addr_t tcp_getIP(TCP* tcp) {
    in_addr_t ip = 0;
    if(tcp->server) {
        if(legacysocket_isBound(&(tcp->super))) {
            legacysocket_getSocketName(&(tcp->super), &ip, NULL);
        } else {
            ip = tcp->server->lastIP;
        }
    } else if(tcp->child) {
        if(legacysocket_isBound(&(tcp->child->parent->super))) {
            legacysocket_getSocketName(&(tcp->child->parent->super), &ip, NULL);
        } else {
            ip = tcp->child->parent->server->lastIP;
        }
    } else {
        legacysocket_getSocketName(&(tcp->super), &ip, NULL);
    }
    return ip;
}

/* Returned address is in network byte order. */
static in_addr_t tcp_getPeerIP(TCP* tcp) {
    in_addr_t ip = tcp->super.peerIP;
    if(tcp->server && ip == 0) {
        ip = tcp->server->lastPeerIP;
    }
    return ip;
}

static guint _tcp_calculateRTT(TCP* tcp, const Host* host) {
    MAGIC_ASSERT(tcp);

    /* addresses are in network byte order */
    in_addr_t sourceIP = tcp_getIP(tcp);
    in_addr_t destinationIP = tcp_getPeerIP(tcp);

    if(sourceIP == htonl(INADDR_ANY)) {
        /* source interface depends on destination */
        if(destinationIP == htonl(INADDR_LOOPBACK)) {
            sourceIP = htonl(INADDR_LOOPBACK);
        } else {
            sourceIP = host_getDefaultIP(host);
        }
    }

    guint rtt = 1;

    if(sourceIP != destinationIP) {
        /* these sim time values are a duration and not an absolute time */
        CSimulationTime srcLatency = worker_getLatency(sourceIP, destinationIP);
        CSimulationTime dstLatency = worker_getLatency(destinationIP, sourceIP);

        /* find latency in milliseconds */
        guint sendLatency = (guint)ceil((gdouble)srcLatency / SIMTIME_ONE_MILLISECOND);
        guint receiveLatency = (guint)ceil((gdouble)dstLatency / SIMTIME_ONE_MILLISECOND);

        if(sendLatency == 0 || receiveLatency == 0) {
            utility_panic("need nonzero latency to set buffer sizes, "
                          "send=%" G_GUINT32_FORMAT " recv=%" G_GUINT32_FORMAT,
                          sendLatency, receiveLatency);
        }
        utility_debugAssert(sendLatency > 0 && receiveLatency > 0);

        rtt = sendLatency + receiveLatency;
    }

    return rtt;
}

static gsize _tcp_computeRTTMEM(TCP* tcp, const Host* host, gboolean isRMEM) {
    gsize bw_KiBps = 0;
    if(isRMEM) {
        bw_KiBps = (gsize)host_get_bw_down_kiBps(host);
    } else {
        bw_KiBps = (gsize)host_get_bw_up_kiBps(host);
    }

    gsize bw_Bps = bw_KiBps * 1024;
    gdouble rttSeconds = ((gdouble)tcp->timing.rttSmoothed) / ((gdouble)1000);

    gsize mem = (gsize)(bw_Bps * rttSeconds);
    return mem;
}

static gsize _tcp_computeMaxRMEM(TCP* tcp, const Host* host) {
    gsize mem = _tcp_computeRTTMEM(tcp, host, TRUE);
    mem = CLAMP(mem, CONFIG_TCP_RMEM_MAX, CONFIG_TCP_RMEM_MAX*10);
    return mem;
}

static gsize _tcp_computeMaxWMEM(TCP* tcp, const Host* host) {
    gsize mem = _tcp_computeRTTMEM(tcp, host, FALSE);
    mem = CLAMP(mem, CONFIG_TCP_WMEM_MAX, CONFIG_TCP_WMEM_MAX*10);
    return mem;
}

static void _tcp_tuneInitialBufferSizes(TCP* tcp, const Host* host) {
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
    tcp->autotune.didInitializeBufferSizes = TRUE;

    /* addresses are in network byte order */
    in_addr_t sourceIP = tcp_getIP(tcp);
    in_addr_t destinationIP = tcp_getPeerIP(tcp);

    if(sourceIP == htonl(INADDR_ANY)) {
        /* source interface depends on destination */
        if(destinationIP == htonl(INADDR_LOOPBACK)) {
            sourceIP = htonl(INADDR_LOOPBACK);
        } else {
            sourceIP = host_getDefaultIP(host);
        }
    }

    if(sourceIP == destinationIP) {
        /* 16 MiB as max */
        gsize inSize = legacysocket_getInputBufferSize(&(tcp->super));
        gsize outSize = legacysocket_getOutputBufferSize(&(tcp->super));

        /* localhost always gets adjusted unless user explicitly set a set */
        if(!tcp->autotune.userDisabledReceive) {
            legacysocket_setInputBufferSize(&(tcp->super), (gsize) CONFIG_TCP_RMEM_MAX);
            trace("set loopback receive buffer size to %"G_GSIZE_FORMAT, (gsize)CONFIG_TCP_RMEM_MAX);
        }
        if(!tcp->autotune.userDisabledSend) {
            legacysocket_setOutputBufferSize(&(tcp->super), (gsize) CONFIG_TCP_WMEM_MAX);
            trace("set loopback send buffer size to %"G_GSIZE_FORMAT, (gsize)CONFIG_TCP_WMEM_MAX);
        }

        tcp->info.rtt = G_MAXUINT32; // not sure why this is here
        return;
    }

    guint32 rtt_milliseconds = (guint32)_tcp_calculateRTT(tcp, host);

    /* i got delay, now i need values for my send and receive buffer
     * sizes based on bandwidth in both directions. do my send size first. */
    guint32 my_send_bw = worker_getBandwidthUpBytes(sourceIP) / 1024;
    guint32 their_receive_bw = worker_getBandwidthDownBytes(destinationIP) / 1024;

    /* KiBps is the same as Bpms, which works with our RTT calculation. */
    guint32 send_bottleneck_bw = my_send_bw < their_receive_bw ? my_send_bw : their_receive_bw;

    /* the delay bandwidth product is how many bytes I can send at once to keep the pipe full */
    guint64 sendbuf_size = (guint64) ((rtt_milliseconds * send_bottleneck_bw * 1024.0f * 1.25f) / 1000.0f);

    /* now the same thing for my receive buf */
    guint32 my_receive_bw = worker_getBandwidthDownBytes(sourceIP) / 1024;
    guint32 their_send_bw = worker_getBandwidthUpBytes(destinationIP) / 1024;

    /* KiBps is the same as Bpms, which works with our RTT calculation. */
    guint32 receive_bottleneck_bw = my_receive_bw < their_send_bw ? my_receive_bw : their_send_bw;

    /* the delay bandwidth product is how many bytes I can receive at once to keep the pipe full */
    guint64 receivebuf_size = (guint64) ((rtt_milliseconds * receive_bottleneck_bw * 1024.0f * 1.25f) / 1000.0f);

    /* keep minimum buffer size bounds */
    sendbuf_size = CLAMP(sendbuf_size, CONFIG_SEND_BUFFER_MIN_SIZE, CONFIG_TCP_WMEM_MAX);
    receivebuf_size = CLAMP(receivebuf_size, CONFIG_RECV_BUFFER_MIN_SIZE, CONFIG_TCP_RMEM_MAX);

    /* check to see if the node should set buffer sizes via autotuning, or
     * they were specified by configuration or parameters in XML */
    if (!tcp->autotune.userDisabledReceive && host_autotuneReceiveBuffer(host)) {
        legacysocket_setInputBufferSize(&(tcp->super), (gsize) receivebuf_size);
    }
    if (!tcp->autotune.userDisabledSend && host_autotuneSendBuffer(host)) {
        tcp->super.outputBufferSize = sendbuf_size;
        legacysocket_setOutputBufferSize(&(tcp->super), (gsize) sendbuf_size);
    }

    debug("set network buffer sizes: send %" G_GSIZE_FORMAT " receive %" G_GSIZE_FORMAT,
          legacysocket_getOutputBufferSize(&(tcp->super)),
          legacysocket_getInputBufferSize(&(tcp->super)));
}

static void _tcp_autotuneReceiveBuffer(TCP* tcp, const Host* host, guint bytesCopied) {
    MAGIC_ASSERT(tcp);

    tcp->autotune.bytesCopied += (gsize)bytesCopied;
    gsize space = 2 * tcp->autotune.bytesCopied;
    space = MAX(space, tcp->autotune.space);

    gsize currentSize = legacysocket_getInputBufferSize(&tcp->super);
    if(space > currentSize) {
        tcp->autotune.space = space;

        gsize newSize = (gsize)MIN(space, _tcp_computeMaxRMEM(tcp, host));
        if(newSize > currentSize) {
            legacysocket_setInputBufferSize(&tcp->super, newSize);
            trace("[autotune] input buffer size adjusted from %"G_GSIZE_FORMAT" to %"G_GSIZE_FORMAT,
                    currentSize, newSize);
        }
    }

    CEmulatedTime now = worker_getCurrentEmulatedTime();
    if(tcp->autotune.lastAdjustment == 0) {
        tcp->autotune.lastAdjustment = now;
    } else if(tcp->timing.rttSmoothed > 0) {
        CSimulationTime threshold =
            ((CSimulationTime)tcp->timing.rttSmoothed) * ((CSimulationTime)SIMTIME_ONE_MILLISECOND);
        if((now - tcp->autotune.lastAdjustment) > threshold) {
            tcp->autotune.lastAdjustment = now;
            tcp->autotune.bytesCopied = 0;
        }
    }
}

static void _tcp_autotuneSendBuffer(TCP* tcp, const Host* host) {
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
    gsize demanded = (gsize)tcp->cong.cwnd;

    gsize newSize = (gsize)MIN((gsize)(sndmem * 2 * demanded), _tcp_computeMaxWMEM(tcp, host));

    gsize currentSize = legacysocket_getOutputBufferSize(&tcp->super);
    if(newSize > currentSize) {
        legacysocket_setOutputBufferSize(&tcp->super, newSize);
        trace("[autotune] output buffer size adjusted from %"G_GSIZE_FORMAT" to %"G_GSIZE_FORMAT,
                currentSize, newSize);
    }
}

void tcp_disableSendBufferAutotuning(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    tcp->autotune.userDisabledSend = TRUE;
}

void tcp_disableReceiveBufferAutotuning(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    tcp->autotune.userDisabledReceive = TRUE;
}

// XXX declaration
static void _tcp_runCloseTimerExpiredTask(const Host* host, gpointer tcp, gpointer userData);
static void _tcp_clearRetransmit(TCP* tcp, guint sequence);

static void _tcp_setState(TCP* tcp, const Host* host, enum TCPState state) {
    MAGIC_ASSERT(tcp);

    tcp->stateLast = tcp->state;
    tcp->state = state;

    trace("%s <-> %s: moved from TCP state '%s' to '%s'", tcp->super.boundString, tcp->super.peerString,
            _tcp_stateToAscii(tcp->stateLast), _tcp_stateToAscii(tcp->state));

    /* some state transitions require us to update the descriptor status */
    switch (state) {
        case TCPS_LISTEN: {
            legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_ACTIVE, TRUE, 0);
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
            legacyfile_adjustStatus(
                (LegacyFile*)tcp, STATUS_FILE_ACTIVE | STATUS_FILE_WRITABLE, TRUE, 0);
            break;
        }
        case TCPS_CLOSING: {
            break;
        }
        case TCPS_CLOSEWAIT: {
            break;
        }
        case TCPS_CLOSED: {
            _tcp_clearRetransmit(tcp, (guint)-1);

            /* user can no longer use socket */
            legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_ACTIVE, FALSE, 0);

            bool disassociate = true;

            in_addr_t sock_ip = 0;
            in_port_t sock_port = 0;
            if (!legacysocket_getSocketName(&tcp->super, &sock_ip, &sock_port)) {
                /* socket isn't bound, so don't try to disassociate */
                disassociate = false;
            }

            in_addr_t peer_ip = 0;
            in_port_t peer_port = 0;
            legacysocket_getPeerName(&tcp->super, &peer_ip, &peer_port);

            /*
             * servers have to wait for all children to close.
             * children need to notify their parents when closing.
             */
            if (!tcp->server || !tcp->server->children ||
                g_hash_table_size(tcp->server->children) <= 0) {
                if(tcp->child && tcp->child->parent) {
                    TCP* parent = tcp->child->parent;
                    utility_debugAssert(parent->server);

                    /* tell my server to stop accepting packets for me
                     * this will destroy the child and NULL out tcp->child */
                    g_hash_table_remove(parent->server->children, &(tcp->child->key));

                    /* if i was the server's last child and its waiting to close, close it */
                    if((parent->state == TCPS_CLOSED) && (g_hash_table_size(parent->server->children) <= 0)) {
                        if (disassociate) {
                            /* this will unbind from the network interface and free socket */
                            host_disassociateInterface(
                                host, PTCP, sock_ip, sock_port, peer_ip, peer_port);
                        }
                    }
                }

                if (disassociate) {
                    /* TODO: we should only be disassociating non-child sockets */
                    host_disassociateInterface(host, PTCP, sock_ip, sock_port, peer_ip, peer_port);
                }
            }
            break;
        }
        case TCPS_LASTACK: {
            /* now as soon as I receive an acknowledgement of my FIN, I close */
            break;
        }
        case TCPS_TIMEWAIT: {
            /* schedule a close timer self-event to finish out the closing process */
            utility_alwaysAssert(tcp->rustSocket != NULL);
            const InetSocket* inetSocket = inetsocketweak_upgrade(tcp->rustSocket);
            utility_alwaysAssert(inetSocket != NULL);
            TaskRef* closeTask =
                taskref_new_bound(host_getID(host), _tcp_runCloseTimerExpiredTask,
                                  (void*)inetSocket, NULL, inetsocket_dropVoid, NULL);
            CSimulationTime delay = CONFIG_TCPCLOSETIMER_DELAY;

            /* if a child of a server initiated the close, close more quickly */
            if(tcp->child && tcp->child->parent) {
                delay = SIMTIME_ONE_SECOND;
            }

            host_scheduleTaskWithDelay(host, closeTask, delay);
            taskref_drop(closeTask);
            break;
        }
        default:
            break;
    }
}

static void _tcp_runCloseTimerExpiredTask(const Host* host, gpointer voidInetSocket,
                                          gpointer userData) {
    const InetSocket* inetSocket = voidInetSocket;
    utility_alwaysAssert(inetSocket != NULL);
    TCP* tcp = inetsocket_asLegacyTcp(inetSocket);
    MAGIC_ASSERT(tcp);

    _tcp_setState(tcp, host, TCPS_CLOSED);
}

/* returns the total amount of buffered data in this TCP socket, including TCP-specific buffers */
gsize tcp_getOutputBufferLength(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    /* this does not include the socket output buffer to avoid double counting, since the
     * data in the socket output buffer is already counted as part of the tcp retransmit queue */
    return tcp->throttledOutputLength + tcp->retransmit.queueLength;
}

/* returns the total amount of buffered data in this TCP socket, including TCP-specific buffers */
gsize tcp_getInputBufferLength(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    return legacysocket_getInputBufferLength(&(tcp->super)) + tcp->unorderedInputLength;
}

/* returns the total number of bytes that we have not yet sent out into the network */
gsize tcp_getNotSentBytes(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    return tcp->throttledOutputLength;
}

static gsize _tcp_getBufferSpaceOut(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    /* account for throttled and retransmission buffer */
    gssize s =
        (gssize)(legacysocket_getOutputBufferSpace(&(tcp->super)) - tcp_getOutputBufferLength(tcp));
    gsize space = (gsize) MAX(0, s);
    return space;
}

static gsize _tcp_getBufferSpaceIn(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    /* account for unordered input buffer */
    gssize space =
        (gssize)(legacysocket_getInputBufferSpace(&(tcp->super)) - tcp->unorderedInputLength);
    return MAX(0, space);
}

static void _tcp_bufferPacketOut(TCP* tcp, Packet* packet) {
    MAGIC_ASSERT(tcp);

    if(!priorityqueue_find(tcp->throttledOutput, packet)) {
        /* TCP wants to avoid congestion */
        priorityqueue_push(tcp->throttledOutput, packet);
        packet_ref(packet);

        /* the packet takes up more space */
        tcp->throttledOutputLength += packet_getPayloadSize(packet);
        if(_tcp_getBufferSpaceOut(tcp) == 0) {
            legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, FALSE, 0);
        }

        packet_addDeliveryStatus(packet, PDS_SND_TCP_ENQUEUE_THROTTLED);
    }
}

static void _tcp_bufferPacketIn(TCP* tcp, Packet* packet) {
    MAGIC_ASSERT(tcp);

    // Don't store old packets whose data we already gave to the plugin.
    PacketTCPHeader* hdr = packet_getTCPHeader(packet);
    bool already_received = hdr->sequence < tcp->receive.next;

    if (!already_received && !priorityqueue_find(tcp->unorderedInput, packet)) {
        /* TCP wants in-order data */
        priorityqueue_push(tcp->unorderedInput, packet);
        packet_ref(packet);

        /* account for the packet length */
        tcp->unorderedInputLength += packet_getPayloadSize(packet);

        packet_addDeliveryStatus(packet, PDS_RCV_TCP_ENQUEUE_UNORDERED);
    }
}

static void _tcp_updateReceiveWindow(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    /* the receive window is how much we are willing to accept to our input buffer.
     * unordered input packets should count against buffer space, so use the _tcp version. */
    //gsize space = _tcp_getBufferSpaceIn(tcp); // causes throughput problems
    gsize space = legacysocket_getInputBufferSpace(&(tcp->super));
    gsize nPackets = space / CONFIG_TCP_MAX_SEGMENT_SIZE;
    tcp->receive.window = nPackets;

    /* handle window updates */
    if(tcp->receive.window == 0) {
        /* we must ensure that we never advertise a 0 window if there is no way
         * for the client to drain the input buffer to further open the window.
         * otherwise, we may get into a deadlock situation where we never accept
         * any packets and the client never reads. */
        utility_debugAssert(!(legacysocket_getInputBufferLength(&(tcp->super)) == 0));
        debug("%s <-> %s: receive window is 0, we have space for %" G_GSIZE_FORMAT
              " bytes in the input buffer",
              tcp->super.boundString, tcp->super.peerString, space);
    }
}

static void _tcp_updateSendWindow(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    /* send window is minimum of congestion window and the last advertised window */
    tcp->send.window = (guint32)MIN(tcp->cong.cwnd, (gint)tcp->receive.lastWindow);
}

static Packet* _tcp_createPacketWithoutPayload(TCP* tcp, const Host* host,
                                               enum ProtocolTCPFlags flags, bool isEmpty) {
    MAGIC_ASSERT(tcp);

    /* packets from children of a server must appear to be coming from the server */

    /* address and port are in network byte order */
    in_addr_t sourceIP = tcp_getIP(tcp);
    in_port_t sourcePort = (tcp->child) ? tcp->child->parent->super.boundPort :
            tcp->super.boundPort;

    /* address and port are in network byte order */
    in_addr_t destinationIP = tcp_getPeerIP(tcp);
    in_port_t destinationPort = (tcp->server) ? tcp->server->lastPeerPort : tcp->super.peerPort;

    if(sourceIP == htonl(INADDR_ANY)) {
        /* source interface depends on destination */
        if(destinationIP == htonl(INADDR_LOOPBACK)) {
            sourceIP = htonl(INADDR_LOOPBACK);
        } else {
            sourceIP = host_getDefaultIP(host);
        }
    }

    utility_debugAssert(sourceIP && sourcePort && destinationIP && destinationPort);

    /* make sure our receive window is up to date before putting it in the packet */
    _tcp_updateReceiveWindow(tcp);

    /* control packets have no sequence number
     * (except SYN and FIN, so we close after sending everything) */
    /* TODO: all FIN packets (including FIN,ACK) should increment the sequence number */
    gboolean isFinNotAck = ((flags & PTCP_FIN) && !(flags & PTCP_ACK));
    guint sequence = !isEmpty || isFinNotAck || (flags & PTCP_SYN) ? tcp->send.next : 0;

    /* create the TCP packet. the ack, window, and timestamps will be set in _tcp_flush */
    Packet* packet = packet_new(host);
    packet_setTCP(packet, flags, sourceIP, sourcePort, destinationIP, destinationPort, sequence);
    packet_addDeliveryStatus(packet, PDS_SND_CREATED);

    /* update sequence number */
    if(sequence > 0) {
        tcp->send.next++;
    }

    return packet;
}

static Packet* _tcp_createDataPacket(TCP* tcp, const Host* host, enum ProtocolTCPFlags flags,
                                     UntypedForeignPtr payload, gsize payloadLength,
                                     const MemoryManager* mem) {
    MAGIC_ASSERT(tcp);

    bool isEmpty = payloadLength == 0;
    Packet* packet = _tcp_createPacketWithoutPayload(tcp, host, flags, isEmpty);
    if (!isEmpty) {
        uint64_t priority = host_getNextPacketPriority(host);
        packet_setPayloadWithMemoryManager(packet, payload, payloadLength, mem, priority);
    }
    return packet;
}

static Packet* _tcp_createControlPacket(TCP* tcp, const Host* host, enum ProtocolTCPFlags flags) {
    MAGIC_ASSERT(tcp);

    return _tcp_createPacketWithoutPayload(tcp, host, flags, /*isEmpty=*/true);
}

static void _tcp_sendControlPacket(TCP* tcp, const Host* host, enum ProtocolTCPFlags flags) {
    MAGIC_ASSERT(tcp);

    trace("%s <-> %s: sending response control packet now",
          tcp->super.boundString, tcp->super.peerString);

    /* create the ack packet, without any payload data */
    Packet* control = _tcp_createControlPacket(tcp, host, flags);

    /* make sure it gets sent before whatever else is in the queue */
    packet_setPriority(control, 0);

    /* push it in the buffer and to the socket */
    _tcp_bufferPacketOut(tcp, control);
    _tcp_flush(tcp, host);

    /* the output buffer holds the packet ref now */
    packet_unref(control);
}

static void _tcp_addRetransmit(TCP* tcp, Packet* packet) {
    MAGIC_ASSERT(tcp);

    PacketTCPHeader* header = packet_getTCPHeader(packet);
    gpointer key = GINT_TO_POINTER(header->sequence);

    /* if it is already in the queue, it won't consume another packet reference */
    if(g_hash_table_lookup(tcp->retransmit.queue, key) == NULL) {
        /* its not in the queue yet */
        g_hash_table_insert(tcp->retransmit.queue, key, packet);
        packet_ref(packet);

        packet_addDeliveryStatus(packet, PDS_SND_TCP_ENQUEUE_RETRANSMIT);

        tcp->retransmit.queueLength += packet_getPayloadSize(packet);
        if(_tcp_getBufferSpaceOut(tcp) == 0) {
            legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, FALSE, 0);
        }
    }
}

static gint _tcp_compare_sequence(gconstpointer ptr_1, gconstpointer ptr_2, gpointer user_data) {
    const guint seq_1 = GPOINTER_TO_INT(ptr_1);
    const guint seq_2 = GPOINTER_TO_INT(ptr_2);
    return (seq_1 < seq_2) ? -1 : (seq_1 > seq_2) ? 1 : 0;
}

/* remove all packets with a sequence number less than the sequence parameter */
static void _tcp_clearRetransmit(TCP* tcp, guint sequence) {
    MAGIC_ASSERT(tcp);

    // Clear the retrans packets in a deterministic order
    GQueue* keys_sorted = g_queue_new();
    GHashTableIter iter;
    gpointer key;

    // First get the keys that need to be removed
    g_hash_table_iter_init(&iter, tcp->retransmit.queue);

    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        guint ackedSequence = GPOINTER_TO_INT(key);
        if(ackedSequence < sequence) {
            g_queue_insert_sorted(keys_sorted, key, _tcp_compare_sequence, NULL);
        }
    }

    // Now remove the packets in order
    while (g_queue_get_length(keys_sorted) > 0) {
        key = g_queue_pop_head(keys_sorted);
        Packet* ackedPacket = g_hash_table_lookup(tcp->retransmit.queue, key);
        if (ackedPacket) {
            tcp->retransmit.queueLength -= packet_getPayloadSize(ackedPacket);
            packet_addDeliveryStatus(ackedPacket, PDS_SND_TCP_DEQUEUE_RETRANSMIT);
            g_hash_table_remove(tcp->retransmit.queue, key);
        }
    }

    // Cleanup
    g_queue_free(keys_sorted);

    if (_tcp_getBufferSpaceOut(tcp) > 0 &&
        (legacyfile_getStatus((LegacyFile*)tcp) & STATUS_FILE_ACTIVE)) {
        legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, TRUE, 0);
    }
}

/* Remove packets in the half-open interval [begin, end) */
static void _tcp_clearRetransmitRange(TCP* tcp, guint begin, guint end) {
    MAGIC_ASSERT(tcp);


    for (uint32_t seq = begin; seq < end; ++seq) {
        Packet *packet = g_hash_table_lookup(tcp->retransmit.queue,
                                             GINT_TO_POINTER(seq));

        if (packet != NULL) {
            tcp->retransmit.queueLength -= packet_getPayloadSize(packet);
            packet_addDeliveryStatus(packet, PDS_SND_TCP_DEQUEUE_RETRANSMIT);
            bool success = g_hash_table_remove(tcp->retransmit.queue,
                                               GINT_TO_POINTER(seq));
            utility_debugAssert(success);
        }
    }

    if (_tcp_getBufferSpaceOut(tcp) > 0 &&
        (legacyfile_getStatus((LegacyFile*)tcp) & STATUS_FILE_ACTIVE)) {
        legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, TRUE, 0);
    }
}

// XXX forward declaration
static void _tcp_runRetransmitTimerExpiredTask(const Host* host, gpointer /*TCP*/ tcp,
                                               gpointer /*Thread*/ thread);

static void _tcp_scheduleRetransmitTimer(TCP* tcp, const Host* host, CSimulationTime now,
                                         CSimulationTime delay) {
    MAGIC_ASSERT(tcp);

    CSimulationTime* expireTimePtr = g_new0(CSimulationTime, 1);
    *expireTimePtr = now + delay;
    gboolean success = priorityqueue_push(tcp->retransmit.scheduledTimerExpirations, expireTimePtr);

    if(success) {
        utility_alwaysAssert(tcp->rustSocket != NULL);
        const InetSocket* inetSocket = inetsocketweak_upgrade(tcp->rustSocket);
        utility_alwaysAssert(inetSocket != NULL);
        TaskRef* retexpTask =
            taskref_new_bound(host_getID(host), _tcp_runRetransmitTimerExpiredTask,
                              (void*)inetSocket, NULL, inetsocket_dropVoid, NULL);
        host_scheduleTaskWithDelay(host, retexpTask, delay);
        taskref_drop(retexpTask);

        trace("%s retransmit timer scheduled for %"G_GUINT64_FORMAT" ns",
                tcp->super.boundString, *expireTimePtr);
    } else {
        warning("%s could not schedule a retransmit timer for %"G_GUINT64_FORMAT" ns",
                tcp->super.boundString, *expireTimePtr);
        g_free(expireTimePtr);
    }
}

static void _tcp_scheduleRetransmitTimerIfNeeded(TCP* tcp, const Host* host, CSimulationTime now) {
    /* logic for scheduling retransmission events. we only need to schedule one if
     * we have no events that will allow us to schedule one later. */
    CSimulationTime* nextTimePtr = priorityqueue_peek(tcp->retransmit.scheduledTimerExpirations);
    if(nextTimePtr && *nextTimePtr <= tcp->retransmit.desiredTimerExpiration) {
        /* another event will fire before the RTO expires, check again then */
        return;
    }

    /* no existing timer will expire as early as desired */
    CSimulationTime delay = tcp->retransmit.desiredTimerExpiration - now;
    _tcp_scheduleRetransmitTimer(tcp, host, now, delay);
}

static void _tcp_setRetransmitTimer(TCP* tcp, const Host* host, CSimulationTime now) {
    MAGIC_ASSERT(tcp);

    /* our retransmission timer needs to change
     * track the new expiration time based on the current RTO */
    CSimulationTime delay = tcp->retransmit.timeout * SIMTIME_ONE_MILLISECOND;
    tcp->retransmit.desiredTimerExpiration = now + delay;

    _tcp_scheduleRetransmitTimerIfNeeded(tcp, host, now);
}

static void _tcp_stopRetransmitTimer(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    /* we want to stop the timer. since there may be an event already scheduled,
     * lets mark our desired time as 0 so we know to cancel when the event fires. */
    tcp->retransmit.desiredTimerExpiration = 0;

    trace("%s retransmit timer disabled", tcp->super.boundString);
}

static void _tcp_setRetransmitTimeout(TCP* tcp, gint newTimeout) {
    MAGIC_ASSERT(tcp);
    tcp->retransmit.timeout = newTimeout;

    /* ensure correct range */
    tcp->retransmit.timeout = MIN(tcp->retransmit.timeout, CONFIG_TCP_RTO_MAX);
    tcp->retransmit.timeout = MAX(tcp->retransmit.timeout, CONFIG_TCP_RTO_MIN);
}

static void _tcp_updateRTTEstimate(TCP* tcp, const Host* host, CSimulationTime timestamp) {
    MAGIC_ASSERT(tcp);

    CSimulationTime now = worker_getCurrentSimulationTime();
    gint rtt = (gint)((now - timestamp) / SIMTIME_ONE_MILLISECOND);

    if(rtt <= 0) {
        rtt = 1;
    }

    /* RFC 6298 (http://tools.ietf.org/html/rfc6298) */
    if(!tcp->timing.rttSmoothed) {
        /* first RTT measurement */
        tcp->timing.rttSmoothed = rtt;
        tcp->timing.rttVariance = rtt / 2;

        if(tcp->autotune.isEnabled && !tcp->autotune.didInitializeBufferSizes) {
            _tcp_tuneInitialBufferSizes(tcp, host);
        }
    } else {
        /* RTTVAR = (1 - beta) * RTTVAR + beta * |SRTT - R|   (beta = 1/4) */
        tcp->timing.rttVariance = (3 * tcp->timing.rttVariance / 4) +
                (ABS(tcp->timing.rttSmoothed - rtt) / 4);
        /* SRTT = (1 - alpha) * SRTT + alpha * R   (alpha = 1/8) */
        tcp->timing.rttSmoothed = (7 * tcp->timing.rttSmoothed / 8) + (rtt / 8);
    }

    /* RTO = SRTT + 4 * RTTVAR  (min=1s, max=60s) */
    gint newRTO = tcp->timing.rttSmoothed + (4 * tcp->timing.rttVariance);
    // fprintf(stderr, "newRTO - %d\n", newRTO);
    _tcp_setRetransmitTimeout(tcp, newRTO);

    trace("srtt=%d rttvar=%d rto=%d", tcp->timing.rttSmoothed,
            tcp->timing.rttVariance, tcp->retransmit.timeout);
}

static void _tcp_retransmitPacket(TCP* tcp, const Host* host, gint sequence) {
    MAGIC_ASSERT(tcp);

    Packet* packet = g_hash_table_lookup(tcp->retransmit.queue, GINT_TO_POINTER(sequence));
    /* if packet wasn't found is was most likely retransmitted from a previous SACK
     * but has yet to be received/acknowledged by the receiver */
    if(!packet) {
        _rswlog(tcp, "Packet %d not in ReTX queue\n", sequence);
        return;
    }

    PacketTCPHeader* hdr = packet_getTCPHeader(packet);

    trace("retransmitting packet %d", sequence);
    // fprintf(stderr, "R- retransmitting packet %d with ts %llu\n", sequence, hdr.timestampValue);

    /* remove from queue and update length and status.
     * calling steal means that the packet ref count is not decremented */
    g_hash_table_steal(tcp->retransmit.queue, GINT_TO_POINTER(sequence));

    /* update queue length and status */
    tcp->retransmit.queueLength -= packet_getPayloadSize(packet);
    packet_addDeliveryStatus(packet, PDS_SND_TCP_DEQUEUE_RETRANSMIT);

    if (_tcp_getBufferSpaceOut(tcp) > 0 &&
        (legacyfile_getStatus((LegacyFile*)tcp) & STATUS_FILE_ACTIVE)) {
        legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, TRUE, 0);
    }

    /* reset retransmit timer since we are resending it now */
    _tcp_setRetransmitTimer(tcp, host, worker_getCurrentSimulationTime());

    /* queue it for sending */
    _tcp_bufferPacketOut(tcp, packet);
    packet_addDeliveryStatus(packet, PDS_SND_TCP_RETRANSMITTED);
    tcp->info.retransmitCount++;

    /* free the ref that we stole */
    packet_unref(packet);
}

static void _tcp_sendShutdownFin(TCP* tcp, const Host* host) {
    MAGIC_ASSERT(tcp);

    gboolean sendFin = FALSE;
    if(tcp->state == TCPS_ESTABLISHED || tcp->state == TCPS_SYNRECEIVED) {
        _tcp_setState(tcp, host, TCPS_FINWAIT1);
        sendFin = TRUE;
    } else if(tcp->state == TCPS_CLOSEWAIT) {
        _tcp_setState(tcp, host, TCPS_LASTACK);
        sendFin = TRUE;
    }

    if(sendFin) {
        /* send a fin */
        Packet* fin = _tcp_createControlPacket(tcp, host, PTCP_FIN);
        _tcp_bufferPacketOut(tcp, fin);
        _tcp_flush(tcp, host);

        /* the output buffer holds the packet ref now */
        packet_unref(fin);
    }
}

void tcp_networkInterfaceIsAboutToSendPacket(TCP* tcp, const Host* host, Packet* packet) {
    MAGIC_ASSERT(tcp);

    CSimulationTime now = worker_getCurrentSimulationTime();

    /* update TCP header to our current advertised window and acknowledgment and timestamps */
    packet_updateTCP(packet, tcp->receive.next, tcp->send.selectiveACKs, tcp->receive.window, 0,
                     false, now, tcp->receive.lastTimestamp);

    /* keep track of the last things we sent them */
    tcp->send.lastAcknowledgment = tcp->receive.next;
    tcp->send.lastWindow = tcp->receive.window;
    tcp->info.lastAckSent = now;

    PacketTCPHeader* header = packet_getTCPHeader(packet);

    if(header->flags & PTCP_ACK) {
        /* we are sending an ACK already, so we may not need any delayed ACK */
        tcp->send.delayedACKCounter = 0;
    }

    if(header->sequence > 0) {
        /* store in retransmission buffer */
        _tcp_addRetransmit(tcp, packet);

        /* start retransmit timer if its not running (rfc 6298, section 5.1) */
        if(!tcp->retransmit.desiredTimerExpiration) {
            _tcp_setRetransmitTimer(tcp, host, now);
        }
    }
}

static void _tcp_flush(TCP* tcp, const Host* host) {
    MAGIC_ASSERT(tcp);

    /* make sure our information is up to date */
    _tcp_updateReceiveWindow(tcp);
    _tcp_updateSendWindow(tcp);

    CSimulationTime now = worker_getCurrentSimulationTime();
    double dtime = (double)(now) / (1.0E9);

    size_t num_lost_ranges =
       retransmit_tally_num_lost_ranges(tcp->retransmit.tally);

    if (num_lost_ranges > 0) {
        uint32_t *lost_ranges = malloc(2 * num_lost_ranges * sizeof(uint32_t));
        retransmit_tally_populate_lost_ranges(tcp->retransmit.tally,
                                              lost_ranges);

        for (size_t idx = 0; idx < num_lost_ranges; ++idx) {
           uint32_t begin = lost_ranges[2*idx];
           uint32_t end = lost_ranges[2*idx + 1];

           _rswlog(tcp, "Retransmitting [%d, %d)\n", begin, end);

           for (uint32_t jdx = begin; jdx < end; ++jdx) {
               // fprintf(stderr, "CW - %s Retransmitting %d @ %f, %zu lost ranges\n", tcp->super.boundString, jdx, dtime, num_lost_ranges);
               _tcp_retransmitPacket(tcp, host, jdx);
           }

           retransmit_tally_mark_retransmitted(tcp->retransmit.tally,
                                               begin, end);

        }

        free(lost_ranges);
    }

    /* find all packets to retransmit and add them throttled output */

    // bool print = true;

    /* flush packets that can now be sent to socket */
    while(!priorityqueue_isEmpty(tcp->throttledOutput)) {
        /* get the next throttled packet, in sequence order */
        Packet* packet = priorityqueue_peek(tcp->throttledOutput);

        /* break out if we have no packets left */
        if(!packet) {
            break;
        }

        gsize length = packet_getPayloadSize(packet);
        PacketTCPHeader* header = packet_getTCPHeader(packet);

        if(length > 0) {
            /* we cant send it if our window is too small */
            gboolean fitsInWindow = (header->sequence < (guint)(tcp->send.unacked + tcp->send.window)) ? TRUE : FALSE;

            /* we cant send it if we dont have enough space */
            gboolean fitsInBuffer =
                (length <= legacysocket_getOutputBufferSpace(&(tcp->super))) ? TRUE : FALSE;

            if(!fitsInBuffer || !fitsInWindow) {
                _rswlog(tcp, "Can't retransmit %d, inWindow=%d, inBuffer=%d\n", header->sequence, fitsInWindow, fitsInBuffer);
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

        /* packet will get stored in retrans queue in tcp_networkInterfaceIsAboutToSendPacket */

        /* socket will queue it ASAP */

        utility_alwaysAssert(tcp->rustSocket != NULL);
        InetSocket* inetSocket = inetsocketweak_upgrade(tcp->rustSocket);
        utility_alwaysAssert(inetSocket != NULL);

        // takes ownership of `inetSocket`, so we don't need to free it
        gboolean success = legacysocket_addToOutputBuffer(&(tcp->super), inetSocket, host, packet);

        tcp->send.packetsSent++;
        tcp->send.highestSequence = (guint32)MAX(tcp->send.highestSequence, (guint)header->sequence);

        _rswlog(tcp, "Sent %d\n", header->sequence);

        /* we already checked for space, so this should always succeed */
        utility_debugAssert(success);
    }

    /* any packets now in order can be pushed to our user input buffer */
    while(!priorityqueue_isEmpty(tcp->unorderedInput)) {
        Packet* packet = priorityqueue_peek(tcp->unorderedInput);

        PacketTCPHeader* header = packet_getTCPHeader(packet);

        _rswlog(tcp, "I just received packet %d\n", header->sequence);
        if (header->sequence < tcp->receive.next) {
            // This is a (probably retransmitted) copy of a packet we already stored
            // and delivered to the plugin.
            trace("Removing packet %u with duplicate data", header->sequence);
            priorityqueue_pop(tcp->unorderedInput);
            tcp->unorderedInputLength -= packet_getPayloadSize(packet);
            packet_unref(packet);
        } else if (header->sequence == tcp->receive.next) {
            /* move from the unordered buffer to user input buffer */
            gboolean fitInBuffer = legacysocket_addToInputBuffer(&(tcp->super), host, packet);

            if(fitInBuffer) {
                // fprintf(stderr, "SND/RCV Recv %s %s %d @ %f\n", tcp->super.boundString, tcp->super.peerString, header.sequence, dtime);
                tcp->receive.lastSequence = header->sequence;
                priorityqueue_pop(tcp->unorderedInput);
                tcp->unorderedInputLength -= packet_getPayloadSize(packet);
                packet_unref(packet);
                (tcp->receive.next)++;
                continue;
            }
        }

        _rswlog(tcp, "Could not buffer %d, was expecting %d\n", header->sequence,
                tcp->receive.next);

        /* we could not buffer it because its out of order or we have no space */
        break;
    }

    /* update the tracker input/output buffer stats */
    Tracker* tracker = host_getTracker(host);
    LegacySocket* socket = (LegacySocket*)tcp;
    gsize inSize = legacysocket_getInputBufferSize(&(tcp->super));
    gsize outSize = legacysocket_getOutputBufferSize(&(tcp->super));
    if (tracker != NULL) {
        CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
        tracker_updateSocketInputBuffer(
            tracker, &compatSocket, inSize - _tcp_getBufferSpaceIn(tcp), inSize);
        tracker_updateSocketOutputBuffer(
            tracker, &compatSocket, outSize - _tcp_getBufferSpaceOut(tcp), outSize);
    }

    /* should we send a fin after clearing the output buffer */
    if((tcp->flags & TCPF_SHOULD_SEND_WR_FIN) && tcp_getOutputBufferLength(tcp) == 0) {
        _tcp_sendShutdownFin(tcp, host);
        tcp->flags &= ~TCPF_SHOULD_SEND_WR_FIN;
    }

    /* check if user needs an EOF signal */
    if((tcp->flags & TCPF_LOCAL_CLOSED_WR) || (tcp->error & TCPE_CONNECTION_RESET)) {
        /* if we closed or conn reset, can't send anymore */
        tcp->error |= TCPE_SEND_EOF;
    }

    /* we said no more reads, or they said no more writes, or reset */
    if((tcp->flags & TCPF_LOCAL_CLOSED_RD) || (tcp->flags & TCPF_REMOTE_CLOSED) ||
            (tcp->error & TCPE_CONNECTION_RESET)) {
        if((tcp->receive.next >= tcp->receive.end) && !(tcp->flags & TCPF_EOF_RD_SIGNALED)) {
            /* user needs to read a 0 so it knows we closed */
            tcp->error |= TCPE_RECEIVE_EOF;
            legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_READABLE, TRUE, 0);
        }
    }

    if((tcp->error & TCPE_CONNECTION_RESET) && (tcp->flags & TCPF_RESET_SIGNALED)) {
        legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, FALSE, 0);
    } else if((tcp->error & TCPE_SEND_EOF) && (tcp->flags & TCPF_EOF_WR_SIGNALED)) {
        legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, FALSE, 0);
    } else if(_tcp_getBufferSpaceOut(tcp) <= 0) {
        legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, FALSE, 0);
    } else if (legacyfile_getStatus((LegacyFile*)tcp) & STATUS_FILE_ACTIVE) {
        legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_WRITABLE, TRUE, 0);
    }
}

static void _tcp_runRetransmitTimerExpiredTask(const Host* host, gpointer voidInetSocket,
                                               gpointer unused) {
    const InetSocket* inetSocket = voidInetSocket;
    utility_alwaysAssert(inetSocket != NULL);
    TCP* tcp = inetsocket_asLegacyTcp(inetSocket);
    MAGIC_ASSERT(tcp);

    /* a timer expired, update our timer tracking state */
    CSimulationTime now = worker_getCurrentSimulationTime();
    CSimulationTime* scheduledTimerExpirationPtr =
        priorityqueue_pop(tcp->retransmit.scheduledTimerExpirations);
    utility_debugAssert(scheduledTimerExpirationPtr);
    g_free(scheduledTimerExpirationPtr);

    trace("%s a scheduled retransmit timer expired", tcp->super.boundString);

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
        _tcp_scheduleRetransmitTimerIfNeeded(tcp, host, now);
        return;
    }

    /* rfc 6298, section 5.4-5.7 (http://tools.ietf.org/html/rfc6298)
     * if we get here, this is a valid timer expiration and we need to do a retransmission
     * do exponential backoff */
    tcp->retransmit.backoffCount++;
    _tcp_setRetransmitTimeout(tcp, tcp->retransmit.timeout * 2);
    _tcp_setRetransmitTimer(tcp, host, now);

    tcp->cong.hooks->tcp_cong_timeout_ev(tcp);
    debug("[CONG] a congestion timeout has occurred on %s", tcp->super.boundString);
    _tcp_logCongestionInfo(tcp);

    retransmit_tally_clear_retransmitted(tcp->retransmit.tally);

    retransmit_tally_mark_lost(tcp->retransmit.tally,
                               tcp->receive.lastAcknowledgment,
                               tcp->send.highestSequence + 1);

    _rswlog(tcp, "Timeout, marking %d as lost.\n", tcp->receive.lastAcknowledgment);

    _tcp_flush(tcp, host);
}

static gboolean _tcp_isFamilySupported(LegacySocket* socket, sa_family_t family) {
    TCP* tcp = _tcp_fromLegacyFile((LegacyFile*)socket);
    MAGIC_ASSERT(tcp);
    return family == AF_INET || family == AF_UNIX ? TRUE : FALSE;
}

/**
 * Check if the TCP socket is a valid listener.
 * returns true if the socket has a configured TCP server and is in LISTEN state, false otherwise
 */
gboolean tcp_isValidListener(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    if(tcp->server && tcp->state == TCPS_LISTEN) {
        return TRUE;
    } else {
        return FALSE;
    }
}

/**
 * Check if the TCP socket allows listening.
 * A socket must not have been used for other purposes to allow listening.
 * returns TRUE if the socket state and flags have not yet been set, FALSE otherwise.
 */
gboolean tcp_isListeningAllowed(TCP* tcp) {
    MAGIC_ASSERT(tcp);
    if ((tcp->state == TCPS_CLOSED || tcp->state == TCPS_LISTEN) && tcp->flags == TCPF_NONE) {
        return TRUE;
    } else {
        return FALSE;
    }
}

gint tcp_getConnectionError(TCP* tcp) {
    MAGIC_ASSERT(tcp);

    if (tcp->flags & TCPF_WAS_ESTABLISHED) {
        /* The 3-way handshake completed at some point. */
        if (tcp->error & TCPE_CONNECTION_RESET) {
            tcp->flags |= TCPF_RESET_SIGNALED;
            return -ECONNRESET;
        }

        if (tcp->state == TCPS_CLOSED) {
            /* Check if we reported a close by returning 0 to the user yet. */
            int readDone =
                tcp->flags & (TCPF_LOCAL_CLOSED_RD | TCPF_EOF_RD_SIGNALED);
            int writeDone =
                tcp->flags & (TCPF_LOCAL_CLOSED_WR | TCPF_EOF_WR_SIGNALED);

            if (readDone && writeDone) {
                return -ENOTCONN;
            }
        }

        /* We are reporting that we are connected. */
        if (tcp->flags & TCPF_CONNECT_SIGNAL_NEEDED) {
            tcp->flags &= ~TCPF_CONNECT_SIGNAL_NEEDED;
            return 0;
        } else {
            return -EISCONN;
        }
    } else {
        /* 3-way handshake has not completed yet. */
        if (tcp->error & TCPE_CONNECTION_RESET) {
            tcp->flags |= TCPF_RESET_SIGNALED;
            return -ECONNREFUSED;
        }

        if (tcp->state == TCPS_SYNSENT || tcp->state == TCPS_SYNRECEIVED) {
            return -EALREADY;
        }

        return 1; // have not sent a SYN yet
    }
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
    tcpinfo->tcpi_snd_mss = (u_int32_t)CONFIG_TCP_MAX_SEGMENT_SIZE;
    tcpinfo->tcpi_rcv_mss = (u_int32_t)CONFIG_TCP_MAX_SEGMENT_SIZE;

    tcpinfo->tcpi_unacked = (u_int32_t)(tcp->send.next - tcp->send.unacked);
//  tcpinfo->tcpi_sacked;
//  tcpinfo->tcpi_lost;
    tcpinfo->tcpi_retrans = (u_int32_t) tcp->info.retransmitCount;
//  tcpinfo->tcpi_fackets;

    /* Times. */
    /* TODO not sure if these are "how long ago the events happened" or an absolute time.
     * They can't possibly be since the epoch, since there are only 32 bits and we are
     * returning microseconds.
     * FIXME If absolute time, these should be the emulated time, not the simulated time.
     */
    tcpinfo->tcpi_last_data_sent = (u_int32_t)(tcp->info.lastDataSent/SIMTIME_ONE_MICROSECOND);
    tcpinfo->tcpi_last_ack_sent = (u_int32_t)(tcp->info.lastAckSent/SIMTIME_ONE_MICROSECOND);
    tcpinfo->tcpi_last_data_recv = (u_int32_t)(tcp->info.lastDataReceived/SIMTIME_ONE_MICROSECOND);
    tcpinfo->tcpi_last_ack_recv = (u_int32_t)(tcp->info.lastAckReceived/SIMTIME_ONE_MICROSECOND);

    /* Metrics. */
    tcpinfo->tcpi_pmtu = (u_int32_t)(CONFIG_MTU);
//  tcpinfo->tcpi_rcv_ssthresh;
    tcpinfo->tcpi_rtt = (u_int32_t)tcp->timing.rttSmoothed;
    tcpinfo->tcpi_rttvar = (u_int32_t)tcp->timing.rttVariance;
    tcpinfo->tcpi_snd_ssthresh = (u_int32_t)tcp->cong.hooks->tcp_cong_ssthresh(tcp);
    tcpinfo->tcpi_snd_cwnd = (u_int32_t)tcp->cong.cwnd;
    tcpinfo->tcpi_advmss = (u_int32_t)CONFIG_TCP_MAX_SEGMENT_SIZE;
    //  tcpinfo->tcpi_reordering;

    tcpinfo->tcpi_rcv_rtt = (u_int32_t)tcp->info.rtt;
    tcpinfo->tcpi_rcv_space = (u_int32_t)tcp->receive.window;

    tcpinfo->tcpi_total_retrans = (u_int32_t)tcp->info.retransmitCount;
}

/* Address and port must be in network byte order. */
static gint _tcp_connectToPeer(LegacySocket* socket, const Host* host, in_addr_t ip, in_port_t port,
                               sa_family_t family) {
    TCP* tcp = _tcp_fromLegacyFile((LegacyFile*)socket);
    MAGIC_ASSERT(tcp);

    /* Only try to connect if we haven't already started. */
    gint errorCode = tcp_getConnectionError(tcp);
    if (errorCode <= 0) {
        return errorCode;
    }

    /* listening sockets can't connect */
    if (tcp_isValidListener(tcp)) {
        return -EISCONN;
    }

    /* send 1st part of 3-way handshake, state->syn_sent */
    _tcp_sendControlPacket(tcp, host, PTCP_SYN);

    trace("%s <-> %s: user initiated connection", tcp->super.boundString, tcp->super.peerString);
    _tcp_setState(tcp, host, TCPS_SYNSENT);

    /* We need to signal when we it succeeds. */
    tcp->flags |= TCPF_CONNECT_SIGNAL_NEEDED;

    /* we dont block, so return EINPROGRESS while waiting for establishment */
    return -EINPROGRESS;
}

void tcp_enterServerMode(TCP* tcp, const Host* host, pid_t process, gint backlog) {
    MAGIC_ASSERT(tcp);

    /* we are a server ready to listen, build our server state */
    tcp->server = _tcpserver_new(backlog, process);

    /* we are now listening for connections */
    _tcp_setState(tcp, host, TCPS_LISTEN);
}

void tcp_updateServerBacklog(TCP* tcp, gint backlog) {
    MAGIC_ASSERT(tcp);
    utility_debugAssert(tcp_isValidListener(tcp));
    _tcpserver_updateBacklog(tcp->server, backlog);
}

/* Address and port must be in network byte order. */
gint tcp_acceptServerPeer(TCP* tcp, const Host* host, in_addr_t* ip, in_port_t* port,
                          gint* acceptedHandle) {
    MAGIC_ASSERT(tcp);
    utility_debugAssert(acceptedHandle);

    /* make sure we are listening and bound to an ip and port */
    if(tcp->state != TCPS_LISTEN || !(tcp->super.flags & SF_BOUND)) {
        return -EINVAL;
    }

    /* we must be a server to accept child connections */
    if(tcp->server == NULL){
        return -EINVAL;
    }

    /* if there are no pending connection ready to accept, dont block waiting */
    if(g_queue_get_length(tcp->server->pending) <= 0) {
        /* listen sockets should have no data, and should not be readable if no pending conns */
        utility_debugAssert(legacysocket_getInputBufferLength(&tcp->super) == 0);
        legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_READABLE, FALSE, 0);
        return -EWOULDBLOCK;
    }

    /* double check the pending child before its accepted */
    TCP* tcpChild = g_queue_pop_head(tcp->server->pending);
    if(!tcpChild) {
        return -ECONNABORTED;
    }

    tcp->server->pendingCount -= 1;

    MAGIC_ASSERT(tcpChild);
    if(tcpChild->error == TCPE_CONNECTION_RESET) {
        return -ECONNABORTED;
    }

    /* better have a peer if we are established */
    utility_debugAssert(tcpChild->super.peerIP && tcpChild->super.peerPort);

    /* child now gets "accepted" */
    MAGIC_ASSERT(tcpChild->child);
    tcpChild->child->state = TCPCS_ACCEPTED;

    /* update child descriptor status */
    legacyfile_adjustStatus(
        &(tcpChild->super.super), STATUS_FILE_ACTIVE | STATUS_FILE_WRITABLE, TRUE, 0);

    /* update server descriptor status */
    if(g_queue_get_length(tcp->server->pending) > 0) {
        legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_READABLE, TRUE, 0);
    } else {
        legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_READABLE, FALSE, 0);
    }

    /* if we're trying to accept the socket from a different process than the process that the
     * socket is registered in (the fd handle won't be correct for this process), then panic to
     * avoid confusing errors later (see https://github.com/shadow/shadow/issues/1780) */
    utility_alwaysAssert(tcpChild->child->parent->server->processForChildren ==
                         process_getProcessID(worker_getCurrentProcess()));

    *acceptedHandle = tcpChild->child->handle;
    /* shouldn't be used anymore */
    tcpChild->child->handle = -1;

    utility_debugAssert(ip);
    *ip = tcpChild->super.peerIP;
    utility_debugAssert(port);
    *port = tcpChild->super.peerPort;

    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL) {
        CompatSocket compatSocket = compatsocket_fromLegacySocket(&tcpChild->child->parent->super);
        tracker_updateSocketPeer(tracker, &compatSocket, *ip, ntohs(tcpChild->super.peerPort));
    }

    return 0;
}

/* Address and port must be in network byte order. */
static TCP* _tcp_getSourceTCP(TCP* tcp, in_addr_t ip, in_port_t port) {
    MAGIC_ASSERT(tcp);

    /* servers may have children keyed by ip:port */
    if(tcp->server) {
        MAGIC_ASSERT(tcp->server);

        /* children are multiplexed based on remote ip and port */
        guint childKey = _ipPortHash(ip, port);
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

    trace("processing data");

    TCPProcessFlags flags = TCP_PF_NONE;
    CSimulationTime now = worker_getCurrentSimulationTime();
    gsize packetLength = packet_getPayloadSize(packet);

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
        if(!isNextPacket && packetFits) {
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

        Status s = legacyfile_getStatus((LegacyFile*)tcp);
        gboolean waitingUserRead = (s & STATUS_FILE_READABLE) ? TRUE : FALSE;

        if((isNextPacket && !waitingUserRead) || (packetFits)) {
            /* make sure its in order */
            _tcp_bufferPacketIn(tcp, packet);
            tcp->info.lastDataReceived = now;
            flags |= TCP_PF_DATA_RECEIVED;
        } else {
            trace("no space for packet even though its in our window");
            packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
        }
    }

    trace("processing data returning flags %i", (int)flags);

    return flags;
}

TCPProcessFlags _tcp_ackProcessing(TCP* tcp, const Host* host, Packet* packet,
                                   PacketTCPHeader* header) {
    MAGIC_ASSERT(tcp);

    trace("processing acks");

    TCPProcessFlags flags = TCP_PF_PROCESSED;
    CSimulationTime now = worker_getCurrentSimulationTime();

    guint32 prevAck = tcp->receive.lastAcknowledgment;
    guint32 prevWin = tcp->receive.lastWindow;

    /* the ack is in our send window */
    gboolean isValidAck = (header->acknowledgment > (guint)tcp->send.unacked) &&
            (header->acknowledgment <= (guint)tcp->send.next);
    /* same ack and window opened, or new ack and window changed */
    gboolean isValidWindow = ((header->acknowledgment == (guint)tcp->receive.lastAcknowledgment) &&
            (header->window > (guint)prevWin)) || ((header->acknowledgment > (guint)tcp->receive.lastAcknowledgment) &&
                    (header->window != (guint)prevWin));

    if(header->window != (guint)prevWin) {
        flags |= TCP_PF_RWND_UPDATED;
    }

    /* duplicate acks indicate out of order data on the other end of connection. */
    bool is_dup = (header->flags & PTCP_DUPACK);

    flags |= retransmit_tally_update(tcp->retransmit.tally,
                                    (guint32)header->acknowledgment,
                                    tcp->send.next, is_dup);

    if (is_dup) {
        debug("[CONG-AVOID] duplicate ack");
        _tcp_logCongestionInfo(tcp);
        tcp->cong.hooks->tcp_cong_duplicate_ack_ev(tcp);
    }

    gint nPacketsAcked = 0;
    if(isValidAck) {
        /* the packets just acked are 'released' from retransmit queue */
        _tcp_clearRetransmitRange(tcp, tcp->receive.lastAcknowledgment,
                                  header->acknowledgment);

        _rswlog(tcp, "The ReTX is now %zu\n", tcp->retransmit.queueLength);

        /* update their advertisements */
        tcp->receive.lastAcknowledgment = (guint32) header->acknowledgment;

        /* some data we sent got acknowledged */
        nPacketsAcked = header->acknowledgment - (guint)tcp->send.unacked;
        tcp->send.unacked = (guint32)header->acknowledgment;

        if(nPacketsAcked > 0) {
            flags |= TCP_PF_DATA_ACKED;

            debug("[CONG] %i packets were acked", nPacketsAcked);
            tcp->cong.hooks->tcp_cong_new_ack_ev(tcp, nPacketsAcked);

            /* increase send buffer size with autotuning */
            if (tcp->autotune.isEnabled && !tcp->autotune.userDisabledSend &&
                host_autotuneSendBuffer(host)) {
                _tcp_autotuneSendBuffer(tcp, host);
            }
        }

        /* if we had congestion, reset our state (rfc 6298, section 5) */
        if(tcp->retransmit.backoffCount > 2) {
            tcp->timing.rttSmoothed = 0;
            tcp->timing.rttVariance = 0;
            _tcp_setRetransmitTimeout(tcp, CONFIG_TCP_RTO_INIT);
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
    } else if(nPacketsAcked > 0) {
        /* new data has been acked */
        _tcp_setRetransmitTimer(tcp, host, now);
    }

    tcp->info.lastAckReceived = now;

    trace("processing acks returning flags %i", (int)flags);

    return flags;
}

static void _tcp_logCongestionInfo(TCP* tcp) {
    gsize outSize = legacysocket_getOutputBufferSize(&tcp->super);
    gsize outLength = legacysocket_getOutputBufferLength(&tcp->super);
    gsize inSize = legacysocket_getInputBufferSize(&tcp->super);
    gsize inLength = legacysocket_getInputBufferLength(&tcp->super);
    double ploss = (double)tcp->info.retransmitCount / tcp->send.packetsSent;

    debug("[CONG-AVOID] cwnd=%d ssthresh=%d rtt=%d "
          "sndbufsize=%" G_GSIZE_FORMAT " sndbuflen=%" G_GSIZE_FORMAT " rcvbufsize=%" G_GSIZE_FORMAT
          " rcbuflen=%" G_GSIZE_FORMAT " "
          "retrans=%" G_GSIZE_FORMAT " ploss=%f desc=%p",
          tcp->cong.cwnd, tcp->cong.hooks->tcp_cong_ssthresh(tcp), tcp->timing.rttSmoothed, outSize,
          outLength, inSize, inLength, tcp->info.retransmitCount, ploss,
          &tcp->super.super);
}

static void _tcp_sendACKTaskCallback(const Host* host, gpointer voidInetSocket, gpointer userData) {
    const InetSocket* inetSocket = voidInetSocket;
    utility_alwaysAssert(inetSocket != NULL);
    TCP* tcp = inetsocket_asLegacyTcp(inetSocket);
    MAGIC_ASSERT(tcp);

    tcp->send.delayedACKIsScheduled = FALSE;
    if(tcp->send.delayedACKCounter > 0) {
        trace("sending a delayed ACK now");
        _tcp_sendControlPacket(tcp, host, PTCP_ACK);
        tcp->send.delayedACKCounter = 0;
    } else {
        trace("delayed ACK was cancelled");
    }
}

/* return TRUE if the packet should be retransmitted */
static void _tcp_processPacket(LegacySocket* socket, const Host* host, Packet* packet) {
    TCP* tcp = _tcp_fromLegacyFile((LegacyFile*)socket);
    MAGIC_ASSERT(tcp);

    /* fetch the TCP info from the packet */
    gsize packetLength = packet_getPayloadSize(packet);

    /* if we run a server, the packet could be for an existing child */
    tcp = _tcp_getSourceTCP(tcp, packet_getSourceIP(packet), packet_getSourcePort(packet));

    /* now we have the true TCP for the packet */
    MAGIC_ASSERT(tcp);
    PacketTCPHeader* header = packet_getTCPHeader(packet);

    /* if packet is reset, don't process */
    if(header->flags & PTCP_RST) {
        /* @todo: not sure if this is handled correctly */
        trace("received RESET packet");

        if(!(tcp->state & TCPS_LISTEN) && !(tcp->error & TCPE_CONNECTION_RESET)) {
            tcp->error |= TCPE_CONNECTION_RESET;
            tcp->flags |= TCPF_REMOTE_CLOSED;

            _tcp_setState(tcp, host, TCPS_TIMEWAIT);

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
        tcp->server->lastPeerIP = header->sourceIP;
        tcp->server->lastPeerPort = header->sourcePort;
        tcp->server->lastIP = header->destinationIP;
    }

    /* go through the state machine, tracking processing and response */
    TCPProcessFlags flags = TCP_PF_NONE;
    enum ProtocolTCPFlags responseFlags = PTCP_NONE;

    trace("processing packet while in state %s", _tcp_stateToAscii(tcp->state));

    switch(tcp->state) {
        case TCPS_LISTEN: {
            /* receive SYN, send SYNACK, move to SYNRECEIVED */
            if(header->flags & PTCP_SYN) {
                MAGIC_ASSERT(tcp->server);

                if (_tcpserver_acceptQueueFull(tcp->server)) {
                    /* no more room, so drop the packet and let client send another SYN later */
                    /* https://blog.cloudflare.com/syn-packet-handling-in-the-wild/#slowapplication
                     */
                    debug("Server socket accept queue is full; dropping SYN packet");
                    packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
                    return;
                }

                flags |= TCP_PF_PROCESSED;

                guint64 recvBufSize = host_getConfiguredRecvBufSize(host);
                guint64 sendBufSize = host_getConfiguredSendBufSize(host);

                /* We will register the child socket with whichever process called listen() on the
                 * parent socket. This is incorrect and we should register the child socket with
                 * whichever process eventually calls accept() on the parent socket, but this is
                 * difficult to fix and isn't an issue until we support fork().
                 * See: https://github.com/shadow/shadow/issues/1780 */
                const Process* registerInProcess =
                    host_getProcess(host, tcp->server->processForChildren);
                if (!registerInProcess) {
                    debug("Listening process no longer exists");
                    packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
                    return;
                }
                /* The descriptor table is stored in the  thread; typically all threads
                 * within a Process share the same one, so using an arbitrary thread
                 * should work. This should be fixed as part of fixing
                 * https://github.com/shadow/shadow/issues/1780.
                 */
                const Thread* registerInThread = process_firstLiveThread(registerInProcess);
                utility_alwaysAssert(registerInThread != NULL);

                /* we need to multiplex a new child */
                TCP* multiplexed = tcp_new(host, recvBufSize, sendBufSize);
                Descriptor* desc = descriptor_fromLegacyTcp(multiplexed, /* flags= */ 0);
                int handle = thread_registerDescriptor(registerInThread, desc);

                multiplexed->child =
                    _tcpchild_new(multiplexed, tcp, handle, header->sourceIP, header->sourcePort);
                utility_debugAssert(
                    g_hash_table_lookup(tcp->server->children, &(multiplexed->child->key)) == NULL);

                /* multiplexed TCP was initialized with a ref of 1, which the host table consumes.
                 * so we need another ref for the children table */
                legacyfile_refWeak(multiplexed);
                g_hash_table_replace(tcp->server->children, &(multiplexed->child->key), multiplexed);

                tcp->server->pendingCount += 1;

                multiplexed->receive.start = header->sequence;
                multiplexed->receive.next = multiplexed->receive.start + 1;

                trace("%s <-> %s: server multiplexed child socket %s <-> %s",
                        tcp->super.boundString, tcp->super.peerString,
                        multiplexed->super.boundString, multiplexed->super.peerString);

                _tcp_setState(multiplexed, host, TCPS_SYNRECEIVED);

                /* child will send response */
                tcp = multiplexed;
                responseFlags = PTCP_SYN|PTCP_ACK;

                trace("new child state %s", _tcp_stateToAscii(tcp->state));
            }
            break;
        }

        case TCPS_SYNSENT: {
            /* receive SYNACK, send ACK, move to ESTABLISHED */
            if((header->flags & PTCP_SYN) && (header->flags & PTCP_ACK)) {
                flags |= TCP_PF_PROCESSED;
                tcp->receive.start = header->sequence;
                tcp->receive.next = tcp->receive.start + 1;

                responseFlags |= PTCP_ACK;
                _tcp_setState(tcp, host, TCPS_ESTABLISHED);
            }
            /* receive SYN, send ACK, move to SYNRECEIVED (simultaneous open) */
            else if(header->flags & PTCP_SYN) {
                flags |= TCP_PF_PROCESSED;
                tcp->receive.start = header->sequence;
                tcp->receive.next = tcp->receive.start + 1;

                responseFlags |= PTCP_ACK;
                _tcp_setState(tcp, host, TCPS_SYNRECEIVED);
            }

            break;
        }

        case TCPS_SYNRECEIVED: {
            /* receive ACK, move to ESTABLISHED */
            if(header->flags & PTCP_ACK) {
                flags |= TCP_PF_PROCESSED;
                _tcp_setState(tcp, host, TCPS_ESTABLISHED);

                /* if this is a child, mark it accordingly */
                if(tcp->child) {
                    tcp->child->state = TCPCS_PENDING;
                    g_queue_push_tail(tcp->child->parent->server->pending, tcp);
                    /* user should accept new child from parent */
                    legacyfile_adjustStatus(
                        &(tcp->child->parent->super.super), STATUS_FILE_READABLE, TRUE, 0);
                }
            }
            break;
        }

        case TCPS_ESTABLISHED: {
            /* receive FIN, send FINACK, move to CLOSEWAIT */
            if(header->flags & PTCP_FIN) {
                flags |= TCP_PF_PROCESSED;

                /* other side of connection closed */
                tcp->flags |= TCPF_REMOTE_CLOSED;
                responseFlags |= (PTCP_FIN|PTCP_ACK);
                _tcp_setState(tcp, host, TCPS_CLOSEWAIT);

                /* remote will send us no more user data after this sequence */
                tcp->receive.end = header->sequence;
            }
            break;
        }

        case TCPS_FINWAIT1: {
            /* receive FINACK, move to FINWAIT2 */
            if((header->flags & PTCP_FIN) && (header->flags & PTCP_ACK)) {
                flags |= TCP_PF_PROCESSED;
                _tcp_setState(tcp, host, TCPS_FINWAIT2);
            }
            /* receive FIN, send FINACK, move to CLOSING (simultaneous close) */
            else if(header->flags & PTCP_FIN) {
                flags |= TCP_PF_PROCESSED;
                responseFlags |= (PTCP_FIN|PTCP_ACK);
                tcp->flags |= TCPF_REMOTE_CLOSED;
                _tcp_setState(tcp, host, TCPS_CLOSING);

                /* it will send no more user data after this sequence */
                tcp->receive.end = header->sequence;
            }
            break;
        }

        case TCPS_FINWAIT2: {
            /* receive FIN, send FINACK, move to TIMEWAIT */
            if(header->flags & PTCP_FIN) {
                flags |= TCP_PF_PROCESSED;
                responseFlags |= (PTCP_FIN|PTCP_ACK);
                tcp->flags |= TCPF_REMOTE_CLOSED;
                _tcp_setState(tcp, host, TCPS_TIMEWAIT);

                /* it will send no more user data after this sequence */
                tcp->receive.end = header->sequence;
            }
            break;
        }

        case TCPS_CLOSING: {
            /* receive FINACK, move to TIMEWAIT */
            if((header->flags & PTCP_FIN) && (header->flags & PTCP_ACK)) {
                flags |= TCP_PF_PROCESSED;
                _tcp_setState(tcp, host, TCPS_TIMEWAIT);
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
            if((header->flags & PTCP_FIN) && (header->flags & PTCP_ACK)) {
                flags |= TCP_PF_PROCESSED;
                _tcp_setState(tcp, host, TCPS_CLOSED);
                /* we closed, cant use tcp anymore */
                trace("packet caused us to close and won't send response");
                return;
            }
            break;
        }

        default:
        case TCPS_CLOSED: {
            /* stray packet, drop without retransmit */
            packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
            trace("already closed and won't send response");
            return;
            break;
        }
    }

    /* listening sockets are not connected and do not exchange data */
    if(tcp->state == TCPS_LISTEN) {
        if(!(flags & TCP_PF_PROCESSED)) {
            packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
        }
        trace("listener does not respond to packets");
        return;
    }

    trace("state after switch is %s", _tcp_stateToAscii(tcp->state));

    /* if TCPE_RECEIVE_EOF, we are not supposed to receive any more */
    if(packetLength > 0 && !(tcp->error & TCPE_RECEIVE_EOF)) {
        flags |= _tcp_dataProcessing(tcp, packet, header);
    }

    if(header->flags & PTCP_ACK) {
        flags |= _tcp_ackProcessing(tcp, host, packet, header);
    }

    /* if it is a spurious packet, drop it */
    if(!(flags & TCP_PF_PROCESSED)) {
        _rswlog(tcp, "Dropping spurious packet %d.\n", header->sequence);
        trace("dropping packet that had no useful info for us");
        utility_debugAssert(responseFlags == PTCP_NONE);
        packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
        return;
    }

    GList* selectiveACKs = packet_copyTCPSelectiveACKs(packet);

    if (selectiveACKs) {
       retransmit_tally_mark_sacked(tcp->retransmit.tally, selectiveACKs);
    }

    if(selectiveACKs) {
        g_list_free(selectiveACKs);
    }

    /* update the last time stamp value (RFC 1323) */
    tcp->receive.lastTimestamp = header->timestampValue;
    if(header->timestampEcho && tcp->retransmit.backoffCount == 0) {
        _tcp_updateRTTEstimate(tcp, host, header->timestampEcho);
    }

    gboolean isAckDubious = ((packetLength == 0) && !(flags & TCP_PF_DATA_ACKED) && !(flags & TCP_PF_RWND_UPDATED)) || (flags & TCP_PF_DATA_SACKED);

    /* see tcp_ack_is_dubious() in net/ipv4/tcp_input.c */
    // gboolean isAckDubious = ((packetLength == 0) && !(flags & TCP_PF_DATA_ACKED) && !(flags & TCP_PF_RWND_UPDATED)) || (flags & TCP_PF_DATA_SACKED);
    gboolean mayRaiseWindow = (tcp->receive.state != TCPRS_RECOVERY);

    if(isAckDubious) {
      // TODO (rwails): Any special handling for dubious acks?
    }

    /* during fast recovery, out of order data results in a duplicate ack.
     * this ack needs to get sent now. */
    if (header->sequence > (guint)tcp->receive.next &&
            (header->sequence < (guint)(tcp->receive.next + tcp->receive.window))) {
        responseFlags |= (PTCP_ACK|PTCP_DUPACK);
    }
    /* otherwise if they sent us new data, we need to ack that we received it.
     * this ack can be delayed. */
    else if(flags & TCP_PF_DATA_RECEIVED) {
        responseFlags |= PTCP_ACK;
    }

    trace("checking if response is needed: flags=%i RCV_EOF=%i FIN=%i",
          (int)responseFlags, (int)(tcp->error & TCPE_RECEIVE_EOF),
          (int)(responseFlags & PTCP_FIN));

    /* send control packet if we have one. we always need to send any packet with a FIN set
     * to ensure the connection close sequence completes on both sides. */
    if(responseFlags != PTCP_NONE &&
            (!(tcp->error & TCPE_RECEIVE_EOF) || (responseFlags & PTCP_FIN))) {
        _rswlog(tcp, "Sending control packet on %d\n",
                header->sequence);

        if(responseFlags != PTCP_ACK) { // includes DUPACKs
            /* just send the response now */
            trace("sending ACK control packet now");
            _tcp_sendControlPacket(tcp, host, responseFlags);
        } else {
            trace("waiting for delayed ACK control packet");
            if(tcp->send.delayedACKIsScheduled == FALSE) {
                /* we need to send an ACK, lets schedule a task so we don't send an ACK
                 * for all packets that are received during this same simtime receiving round. */
                utility_alwaysAssert(tcp->rustSocket != NULL);
                const InetSocket* inetSocket = inetsocketweak_upgrade(tcp->rustSocket);
                utility_alwaysAssert(inetSocket != NULL);
                TaskRef* sendACKTask =
                    taskref_new_bound(host_getID(host), _tcp_sendACKTaskCallback, (void*)inetSocket,
                                      NULL, inetsocket_dropVoid, NULL);

                /* figure out what we should use as delay */
                CSimulationTime delay = 0;
                /* "quick acknowledgments" happen at the beginning of a connection */
                if(tcp->send.numQuickACKsSent < 1000) {
                    /* we want the other side to get the ACKs sooner so we don't throttle its sending rate */
                    delay = 1*SIMTIME_ONE_MILLISECOND;
                    tcp->send.numQuickACKsSent++;
                } else {
                    delay = 5*SIMTIME_ONE_MILLISECOND;
                }

                host_scheduleTaskWithDelay(host, sendACKTask, delay);
                taskref_drop(sendACKTask);

                tcp->send.delayedACKIsScheduled = TRUE;
            }
            tcp->send.delayedACKCounter++;
        }
    }

    /* now flush as many packets as we can to socket */
    _tcp_flush(tcp, host);

    /* clear it so we dont send outdated timestamp echos */
    tcp->receive.lastTimestamp = 0;

    trace("done processing in state %s", _tcp_stateToAscii(tcp->state));
}

static void _tcp_dropPacket(LegacySocket* socket, const Host* host, Packet* packet) {
    TCP* tcp = _tcp_fromLegacyFile((LegacyFile*)socket);
    MAGIC_ASSERT(tcp);

    /* if we run a server, the packet could be for an existing child */
    tcp = _tcp_getSourceTCP(tcp, packet_getDestinationIP(packet), packet_getDestinationPort(packet));

    /* now we have the true TCP for the packet */
    MAGIC_ASSERT(tcp);

    _tcp_flush(tcp, host);
}

static void _tcp_endOfFileSignalled(TCP* tcp, enum TCPFlags flags) {
    MAGIC_ASSERT(tcp);

    trace("%s <-> %s: signaling close to user, socket no longer usable", tcp->super.boundString, tcp->super.peerString);
    tcp->flags |= flags;

    if((tcp->flags & TCPF_EOF_RD_SIGNALED) && (tcp->flags & TCPF_EOF_WR_SIGNALED)) {
        /* user can no longer access socket */
        /* FIXME: a file should not be closed if there are still file handles (fds) to it */
        legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_CLOSED, TRUE, 0);
        legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_ACTIVE, FALSE, 0);
    }
}

/* Address and port must be in network byte order. */
gssize tcp_sendUserData(TCP* tcp, const Host* host, UntypedForeignPtr buffer, gsize nBytes,
                        in_addr_t ip, in_port_t port, const MemoryManager* mem) {
    MAGIC_ASSERT(tcp);

    /* return 0 to signal close, if necessary */
    if(tcp->error & TCPE_SEND_EOF)
    {
        trace("send EOF is set");
        if(tcp->state == TCPS_CLOSED) {
            return -ENOTCONN;
        } else {
            _tcp_endOfFileSignalled(tcp, TCPF_EOF_WR_SIGNALED);
            return -EPIPE;
        }
    }

    /* maximum data we can send network, o/w tcp truncates and only sends 65536*/
    gsize acceptable = MIN(nBytes, 65535);
    gsize space = _tcp_getBufferSpaceOut(tcp);
    gsize remaining = MIN(acceptable, space);

    /* break data into segments and send each in a packet */
    gsize maxPacketLength = CONFIG_TCP_MAX_SEGMENT_SIZE;
    gsize bytesCopied = 0;

    /* Need non-NULL buffer. */
    /* FIXME: should push this check to the point the data is actually read, to correctly handle
     * non-NULL pointers that aren't accessible. This is currently in the Payload code; need to
     * bubble up errors from there. If we do bubble up from the payload code, we also need to undo
     * the TCP state changes made earlier, for example the sequence number increment in the
     * _tcp_createPacketWithoutPayload code.
     */
    if (buffer.val == 0) {
        return -EFAULT;
    }

    /* create as many packets as needed */
    while(remaining > 0) {
        gsize copyLength = MIN(maxPacketLength, remaining);

        /* use helper to create the packet */
        Packet* packet = _tcp_createDataPacket(tcp, host, PTCP_ACK,
                                               (UntypedForeignPtr){.val = buffer.val + bytesCopied},
                                               copyLength, mem);

        if(copyLength > 0) {
            /* we are sending more user data */
            tcp->send.end++;
        }

        /* buffer the outgoing packet in TCP */
        _tcp_bufferPacketOut(tcp, packet);

        /* the output buffer holds the packet ref now */
        packet_unref(packet);

        remaining -= copyLength;
        bytesCopied += copyLength;
    }

    trace("%s <-> %s: sending %"G_GSIZE_FORMAT" user bytes", tcp->super.boundString, tcp->super.peerString, bytesCopied);

    /* now flush as much as possible out to socket */
    _tcp_flush(tcp, host);

    return (gssize)(bytesCopied == 0 && nBytes != 0 ? -EWOULDBLOCK : bytesCopied);
}

static void _tcp_sendWindowUpdate(const Host* host, gpointer voidInetSocket, gpointer data) {
    const InetSocket* inetSocket = voidInetSocket;
    utility_alwaysAssert(inetSocket != NULL);
    TCP* tcp = inetsocket_asLegacyTcp(inetSocket);
    MAGIC_ASSERT(tcp);

    trace("%s <-> %s: receive window opened, advertising the new "
            "receive window %"G_GUINT32_FORMAT" as an ACK control packet",
            tcp->super.boundString, tcp->super.peerString, tcp->receive.window);

    // XXX we may be in trouble if this packet gets dropped
    _tcp_sendControlPacket(tcp, host, PTCP_ACK);

    tcp->receive.windowUpdatePending = FALSE;
}

/* Address and port must be in network byte order. */
gssize tcp_receiveUserData(TCP* tcp, const Host* host, UntypedForeignPtr buffer, gsize nBytes,
                           in_addr_t* ip, in_port_t* port, MemoryManager* mem) {
    MAGIC_ASSERT(tcp);

    /*
     * TODO
     * We call legacyfile_adjustStatus too many times here, to handle the readable
     * state of the socket at times when we have a partially read packet. Consider
     * adding a required hook for socket subclasses so the socket layer can
     * query TCP for readability status.
     */

    /* make sure we pull in all readable user data */
    _tcp_flush(tcp, host);

    gsize remaining = nBytes;
    gsize totalCopied = 0;
    gsize offset = 0;
    gsize copyLength = 0;

    if ((legacysocket_getInputBufferLength(&tcp->super) == 0) &&
        (tcp->partialUserDataPacket == NULL) && !(tcp->error & TCPE_RECEIVE_EOF)) {
        // there is no data, and we have not received an EOF
        return -EWOULDBLOCK;
    }

    if (buffer.val == 0 && nBytes > 0) {
        debug("Can't recv >0 bytes into NULL buffer on socket");
        return -EFAULT;
    }

    /* check if we have a partial packet waiting to get finished */
    if(remaining > 0 && tcp->partialUserDataPacket) {
        gsize partialLength = packet_getPayloadSize(tcp->partialUserDataPacket);
        gsize partialBytes = partialLength - tcp->partialOffset;
        utility_debugAssert(partialBytes > 0);

        copyLength = MIN(partialBytes, remaining);
        gssize bytesCopied = packet_copyPayloadWithMemoryManager(
            tcp->partialUserDataPacket, tcp->partialOffset, buffer, copyLength, mem);
        if (bytesCopied < 0) {
            // Error writing to UntypedForeignPtr
            return bytesCopied;
        }
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
            utility_debugAssert(remaining == 0);
        }
    }

    while(remaining > 0) {
        /* if we get here, we should have read the partial packet above, or
         * broken out below */
        utility_debugAssert(tcp->partialUserDataPacket == NULL);
        utility_debugAssert(tcp->partialOffset == 0);

        /* get the next buffered packet - we'll always need it.
         * this could mark the socket as unreadable if this is its last packet.*/
        const Packet* nextPacket = legacysocket_peekNextInPacket((LegacySocket*)tcp);
        if (!nextPacket) {
            /* no more packets or partial packets */
            break;
        }

        gsize packetLength = packet_getPayloadSize(nextPacket);
        copyLength = MIN(packetLength, remaining);
        gssize bytesCopied = packet_copyPayloadWithMemoryManager(
            nextPacket, 0, (UntypedForeignPtr){.val = buffer.val + offset}, copyLength, mem);
        if (bytesCopied < 0) {
            // Error writing to UntypedForeignPtr
            if (totalCopied > 0) {
                warning("Returning error %s, but already copied %lu bytes which will be lost",
                        g_strerror(-bytesCopied), totalCopied);
            }
            return bytesCopied;
        }
        totalCopied += bytesCopied;
        remaining -= bytesCopied;
        offset += bytesCopied;

        Packet* packet = legacysocket_removeFromInputBuffer((LegacySocket*)tcp, host);

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

    bool more_readable_data = false;

    /* now we update readability of the socket */
    if ((legacysocket_getInputBufferLength(&(tcp->super)) > 0) ||
        (tcp->partialUserDataPacket != NULL)) {
        /* we still have readable data */
        legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_READABLE, TRUE, 0);
        more_readable_data = true;
    } else {
        /* all of our ordered user data has been read */
        if((tcp->unorderedInputLength == 0) && (tcp->error & TCPE_RECEIVE_EOF)) {
            /* there is no more unordered data either, and we need to signal EOF */
            if(totalCopied > 0) {
                /* we just received bytes, so we can't EOF until the next call.
                 * make sure we stay readable so we DO actually EOF the socket */
                legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_READABLE, TRUE, 0);
            } else {
                /* OK, no more data and nothing just received. */
                if(tcp->state == TCPS_CLOSED) {
                    return -ENOTCONN;
                } else {
                    _tcp_endOfFileSignalled(tcp, TCPF_EOF_RD_SIGNALED);
                    return 0;
                }
            }
        } else {
            /* our socket still has unordered data or is still open, but empty for now */
            legacyfile_adjustStatus(&(tcp->super.super), STATUS_FILE_READABLE, FALSE, 0);
        }
    }

    /* update the receive buffer size based on new packets received */
    if(tcp->autotune.isEnabled && !tcp->autotune.userDisabledReceive) {
        if(host_autotuneReceiveBuffer(host)) {
            _tcp_autotuneReceiveBuffer(tcp, host, totalCopied);
        }
    }

    /* if we have advertised a 0 window because the application wasn't reading,
     * we now have to update the window and let the sender know */
    _tcp_updateReceiveWindow(tcp);
    if(tcp->receive.window > tcp->send.lastWindow && !tcp->receive.windowUpdatePending) {
        /* our receive window just opened, make sure the sender knows it can
         * send more. otherwise we get into a deadlock situation!
         * make sure we don't send multiple events when read is called many times per instant */
        utility_alwaysAssert(tcp->rustSocket != NULL);
        const InetSocket* inetSocket = inetsocketweak_upgrade(tcp->rustSocket);
        utility_alwaysAssert(inetSocket != NULL);
        TaskRef* updateWindowTask =
            taskref_new_bound(host_getID(host), _tcp_sendWindowUpdate, (void*)inetSocket, NULL,
                              inetsocket_dropVoid, NULL);
        host_scheduleTaskWithDelay(host, updateWindowTask, 1);
        taskref_drop(updateWindowTask);

        tcp->receive.windowUpdatePending = TRUE;
    }

    trace("%s <-> %s: receiving %" G_GSIZE_FORMAT " user bytes", tcp->super.boundString,
          tcp->super.peerString, totalCopied);

    // only return EWOULDBLOCK if no bytes were copied, and either we requested bytes or there is no
    // more data to read
    if (totalCopied == 0 && (nBytes != 0 || !more_readable_data)) {
        return -EWOULDBLOCK;
    }

    return totalCopied;
}

static void _tcp_cleanup(LegacyFile* descriptor) {
    TCP* tcp = _tcp_fromLegacyFile(descriptor);
    MAGIC_ASSERT(tcp);

    // if we have a parent, we should break any references between it and us
    if (tcp->child) {
        _tcpchild_free(tcp->child);
        tcp->child = NULL;
    }
}

static void _tcp_free(LegacyFile* descriptor) {
    TCP* tcp = _tcp_fromLegacyFile(descriptor);
    MAGIC_ASSERT(tcp);

    priorityqueue_free(tcp->throttledOutput);
    priorityqueue_free(tcp->unorderedInput);
    g_hash_table_destroy(tcp->retransmit.queue);
    priorityqueue_free(tcp->retransmit.scheduledTimerExpirations);

    if (tcp->partialUserDataPacket != NULL) {
        packet_unref(tcp->partialUserDataPacket);
        tcp->partialUserDataPacket = NULL;
        tcp->partialOffset = 0;
    }

    if (tcp->child) {
        _tcpchild_free(tcp->child);
        tcp->child = NULL;
    }

    if(tcp->server) {
        _tcpserver_free(tcp->server);
    }

    tcp->cong.hooks->tcp_cong_delete(tcp);
    retransmit_tally_destroy(tcp->retransmit.tally);

    if (tcp->rustSocket != NULL) {
        inetsocketweak_drop(tcp->rustSocket);
        tcp->rustSocket = NULL;
    }

    legacyfile_clear((LegacyFile*)tcp);
    MAGIC_CLEAR(tcp);
    g_free(tcp);

    worker_count_deallocation(TCP);
}

static void _tcp_close(LegacyFile* descriptor, const Host* host) {
    TCP* tcp = _tcp_fromLegacyFile(descriptor);
    MAGIC_ASSERT(tcp);

    /* We always return FALSE because we handle process deregististration
     * on our own. */

    trace("%s <-> %s:  user closed connection", tcp->super.boundString, tcp->super.peerString);
    tcp->flags |= TCPF_LOCAL_CLOSED_WR;
    tcp->flags |= TCPF_LOCAL_CLOSED_RD;

    /* the user closed the connection, so should never interact with the socket again */
    legacyfile_adjustStatus((LegacyFile*)tcp, STATUS_FILE_ACTIVE, FALSE, 0);

    switch (tcp->state) {
        case TCPS_LISTEN:
        case TCPS_SYNSENT: {
            _tcp_setState(tcp, host, TCPS_CLOSED);
            return;
        }

        case TCPS_SYNRECEIVED:
        case TCPS_ESTABLISHED:
        case TCPS_CLOSEWAIT: {
            if(tcp_getOutputBufferLength(tcp) == 0) {
                _tcp_sendShutdownFin(tcp, host);
            } else {
                /* we still have data. send that first, and then finish with fin */
                tcp->flags |= TCPF_SHOULD_SEND_WR_FIN;
            }
            return;
        }

        case TCPS_FINWAIT1:
        case TCPS_FINWAIT2:
        case TCPS_CLOSING:
        case TCPS_TIMEWAIT:
        case TCPS_LASTACK: {
            /* close was already called, do nothing */
            return;
        }

        default: {
            /* if we didnt start connection yet, we still want to make sure
             * we set the state to closed so we unbind the socket */
            _tcp_setState(tcp, host, TCPS_CLOSED);
            return;
        }
    }
}

gint tcp_shutdown(TCP* tcp, const Host* host, gint how) {
    MAGIC_ASSERT(tcp);

    if(tcp->state == TCPS_SYNSENT || tcp->state == TCPS_SYNRECEIVED ||
            tcp->state == TCPS_LISTEN || tcp->state == TCPS_CLOSED) {
        return -ENOTCONN;
    }

    if(how == SHUT_RD || how == SHUT_RDWR) {
        /* can't receive any more */
        tcp->flags |= TCPF_LOCAL_CLOSED_RD;
        tcp->error |= TCPE_RECEIVE_EOF;
    }

    if((how == SHUT_WR || how == SHUT_RDWR) && !(tcp->flags & TCPF_LOCAL_CLOSED_WR)) {
        /* can't send any more */
        tcp->flags |= TCPF_LOCAL_CLOSED_WR;
        tcp->error |= TCPE_SEND_EOF;

        if(tcp_getOutputBufferLength(tcp) == 0) {
            _tcp_sendShutdownFin(tcp, host);
        } else {
            tcp->flags |= TCPF_SHOULD_SEND_WR_FIN;
        }
    }

    return 0;
}

static gssize _sendUserDataPanic(LegacySocket* socket, const Thread* thread,
                                 UntypedForeignPtr buffer, gsize nBytes, in_addr_t ip,
                                 in_port_t port) {
    /* sending should be handled by the rust `LegacyTcpSocket` wrapper, which should call
     * tcp_sendUserData directly */
    utility_panic("Called `legacysocket_sendUserData` on a TCP socket");
}

static gssize _receiveUserDataPanic(LegacySocket* socket, const Thread* thread,
                                    UntypedForeignPtr buffer, gsize nBytes, in_addr_t* ip,
                                    in_port_t* port) {
    /* receiving should be handled by the rust `LegacyTcpSocket` wrapper, which should call
     * tcp_receiveUserData directly */
    utility_panic("Called `legacysocket_receiveUserData` on a TCP socket");
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable tcp_functions = {_tcp_close,
                                     _tcp_cleanup,
                                     _tcp_free,
                                     _sendUserDataPanic,
                                     _receiveUserDataPanic,
                                     _tcp_processPacket,
                                     _tcp_isFamilySupported,
                                     _tcp_connectToPeer,
                                     _tcp_dropPacket,
                                     MAGIC_VALUE};

/* takes ownership of the `InetSocketWeak` */
void tcp_setRustSocket(TCP* tcp, InetSocketWeak* rustSocket) {
    utility_alwaysAssert(tcp->rustSocket == NULL);
    tcp->rustSocket = rustSocket;
}

TCP* tcp_new(const Host* host, guint receiveBufferSize, guint sendBufferSize) {
    TCP* tcp = g_new0(TCP, 1);
    MAGIC_INIT(tcp);

    legacysocket_init(
        &(tcp->super), host, &tcp_functions, DT_TCPSOCKET, receiveBufferSize, sendBufferSize);

    guint32 initial_window = 10;
    gint tcpSSThresh = 0;

    /* in the future we'd like to support more congestion control types
     * and allow it to be set as a host option */
    tcp_cong_reno_init(tcp);

    tcp->send.window = initial_window;
    tcp->send.lastWindow = initial_window;
    tcp->receive.window = initial_window;
    tcp->receive.lastWindow = initial_window;

    /* 0 is saved for representing control packets */
    guint32 initialSequenceNumber = 1;

    /* the first packet (the SYN packet) has a sequence number of 'initialSequenceNumber' */
    tcp->send.unacked = initialSequenceNumber;
    tcp->send.next = initialSequenceNumber;
    tcp->send.end = initialSequenceNumber;
    tcp->send.lastAcknowledgment = initialSequenceNumber;
    tcp->receive.end = initialSequenceNumber;
    tcp->receive.next = initialSequenceNumber;
    tcp->receive.start = initialSequenceNumber;
    tcp->receive.lastAcknowledgment = initialSequenceNumber;

    tcp->autotune.isEnabled = TRUE;

    tcp->throttledOutput = priorityqueue_new((GCompareDataFunc)packet_compareTCPSequence, NULL,
                                             (GDestroyNotify)packet_unref, NULL, NULL);
    tcp->unorderedInput = priorityqueue_new((GCompareDataFunc)packet_compareTCPSequence, NULL,
                                            (GDestroyNotify)packet_unref, NULL, NULL);
    tcp->retransmit.queue =
            g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)packet_unref);

    retransmit_tally_init(&tcp->retransmit.tally);

    tcp->retransmit.scheduledTimerExpirations =
        priorityqueue_new((GCompareDataFunc)_simulationTimeCompare, NULL, g_free, NULL, NULL);

    /* initialize tcp retransmission timeout */
    _tcp_setRetransmitTimeout(tcp, CONFIG_TCP_RTO_INIT);

    worker_count_allocation(TCP);
    return tcp;
}
