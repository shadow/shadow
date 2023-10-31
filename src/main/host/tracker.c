/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/* a packet is a 'data' packet if it has a payload attached, and a 'control' packet otherwise.
 * each packet is either a 'normal' packet or a 'retransmitted' packet. */
#include <glib.h>
#include <netinet/in.h>
#include <string.h>

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

typedef struct {
    gsize control;
    gsize controlRetransmit;
    gsize data;
    gsize dataRetransmit;
} PacketCounter;

typedef struct {
    gsize controlHeader;
    gsize controlHeaderRetransmit;
    gsize dataHeader;
    gsize dataHeaderRetransmit;
    gsize dataPayload;
    gsize dataPayloadRetransmit;
} ByteCounter;

typedef struct {
    ByteCounter bytes;
    PacketCounter packets;
} Counters;

typedef struct {
    Counters inCounters;
    Counters outCounters;
} IFaceCounters;

struct _Tracker {
    /* our personal settings as configured in the shadow xml config file */
    CSimulationTime interval;
    LogLevel loglevel;
    LogInfoFlags loginfo;

    gboolean didLogNodeHeader;
    gboolean didLogRAMHeader;
    gboolean didLogSocketHeader;

    uint64_t processingTimeTotalNanos;
    uint64_t processingTimeLastIntervalNanos;

    gsize numDelayedTotal;
    CSimulationTime delayTimeTotal;
    gsize numDelayedLastInterval;
    CSimulationTime delayTimeLastInterval;

    IFaceCounters local;
    IFaceCounters remote;

    GHashTable* allocatedLocations;
    gsize allocatedBytesTotal;
    gsize allocatedBytesLastInterval;
    gsize deallocatedBytesLastInterval;
    guint numFailedFrees;

    GHashTable* socketStats;

    CEmulatedTime lastHeartbeat;

    MAGIC_DECLARE;
};

typedef struct _SocketStats SocketStats;
struct _SocketStats {
    /* use the socket's pointer as a unique id/handle */
    guintptr socket;
    ProtocolType type;

    in_addr_t peerIP;
    in_port_t peerPort;
    gchar* peerHostname;

    gsize inputBufferSize;
    gsize inputBufferLength;
    gsize outputBufferSize;
    gsize outputBufferLength;

    IFaceCounters local;
    IFaceCounters remote;

    gboolean removeAfterNextLog;

    MAGIC_DECLARE;
};

static SocketStats* _socketstats_new(guintptr socket, ProtocolType type, gsize inputBufferSize,
                                     gsize outputBufferSize) {
    SocketStats* ss = g_new0(SocketStats, 1);

    ss->socket = socket;
    ss->type = type;
    ss->inputBufferSize = inputBufferSize;
    ss->outputBufferSize = outputBufferSize;
    ss->peerHostname = g_strdup("UNSPEC");

    return ss;
}

static void _socketstats_free(SocketStats* ss) {
    if(ss) {
        if(ss->peerHostname) {
            g_free(ss->peerHostname);
        }
        g_free(ss);
    }
}

static guintptr _tracker_socketHandle(const CompatSocket* sock) {
    return compatsocket_getCanonicalHandle(sock);
}

Tracker* tracker_new(const Host* host, CSimulationTime interval, LogLevel loglevel,
                     LogInfoFlags loginfo) {
    Tracker* tracker = g_new0(Tracker, 1);
    MAGIC_INIT(tracker);

    tracker->interval = interval;
    tracker->loglevel = loglevel;
    tracker->loginfo = loginfo;

    tracker->allocatedLocations = g_hash_table_new(g_direct_hash, g_direct_equal);
    tracker->socketStats = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)_socketstats_free);

    /* send an alive message, and start periodic heartbeats */
    tracker_heartbeat(tracker, host);

    return tracker;
}

static void _tracker_freeAllocatedLocations(gpointer key, gpointer value, gpointer userData) {
    if(key) {
        g_free(key);
    }
}

void tracker_free(Tracker* tracker) {
    MAGIC_ASSERT(tracker);

    g_hash_table_foreach(tracker->allocatedLocations, _tracker_freeAllocatedLocations, NULL);
    g_hash_table_destroy(tracker->allocatedLocations);
    g_hash_table_destroy(tracker->socketStats);

    MAGIC_CLEAR(tracker);
    g_free(tracker);
}

void tracker_addProcessingTimeNanos(Tracker* tracker, uint64_t processingTimeNanos) {
    MAGIC_ASSERT(tracker);

    if(tracker->loginfo & LOG_INFO_FLAGS_NODE) {
        tracker->processingTimeTotalNanos += processingTimeNanos;
        tracker->processingTimeLastIntervalNanos += processingTimeNanos;
    }
}

void tracker_addVirtualProcessingDelay(Tracker* tracker, CSimulationTime delay) {
    MAGIC_ASSERT(tracker);

    if(tracker->loginfo & LOG_INFO_FLAGS_NODE) {
        (tracker->numDelayedTotal)++;
        tracker->delayTimeTotal += delay;
        (tracker->numDelayedLastInterval)++;
        tracker->delayTimeLastInterval += delay;
    }
}

static void _tracker_updateCounters(Counters* c, gsize header, gsize payload,
        PacketDeliveryStatusFlags status) {
    if(!c) {
        return;
    }

    if(payload > 0) {
        /* this is a 'data' packet */
        if(status & PDS_SND_TCP_RETRANSMITTED) {
            /* this is a retransmitted 'data' packet */
            c->packets.dataRetransmit++;
            c->bytes.dataHeaderRetransmit += header;
            c->bytes.dataPayloadRetransmit += payload;
        } else {
            /* this is a first-transmitted 'data' packet */
            c->packets.data++;
            c->bytes.dataHeader += header;
            c->bytes.dataPayload += payload;
        }
    } else {
        /* this is a 'control' packet */
        if(status & PDS_SND_TCP_RETRANSMITTED) {
            /* this is a retransmitted 'control' packet */
            c->packets.controlRetransmit++;
            c->bytes.controlHeaderRetransmit += header;
        } else {
            /* this is a first-transmitted 'control' packet */
            c->packets.control++;
            c->bytes.controlHeader += header;
        }
    }
}

void tracker_addInputBytes(Tracker* tracker, Packet* packet, const CompatSocket* socket) {
    MAGIC_ASSERT(tracker);
    guintptr handle = _tracker_socketHandle(socket);

    if(!(tracker->loginfo & LOG_INFO_FLAGS_NODE) && !(tracker->loginfo & LOG_INFO_FLAGS_SOCKET)) {
        return;
    }

    gboolean isLocal = packet_getDestinationIP(packet) == htonl(INADDR_LOOPBACK);
    gsize header = packet_getHeaderSize(packet);
    gsize payload = packet_getPayloadSize(packet);
    PacketDeliveryStatusFlags status = packet_getDeliveryStatus(packet);

    if(tracker->loginfo & LOG_INFO_FLAGS_NODE) {
        if(isLocal) {
            _tracker_updateCounters(&tracker->local.inCounters, header, payload, status);
        } else {
            _tracker_updateCounters(&tracker->remote.inCounters, header, payload, status);
        }
    }

    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            if(isLocal) {
                _tracker_updateCounters(&ss->local.inCounters, header, payload, status);
            } else {
                _tracker_updateCounters(&ss->remote.inCounters, header, payload, status);
            }
        }
    }
}

void tracker_addOutputBytes(Tracker* tracker, Packet* packet, const CompatSocket* socket) {
    MAGIC_ASSERT(tracker);
    guintptr handle = _tracker_socketHandle(socket);

    if(!(tracker->loginfo & LOG_INFO_FLAGS_NODE) && !(tracker->loginfo & LOG_INFO_FLAGS_SOCKET)) {
        return;
    }

    gboolean isLocal = packet_getSourceIP(packet) == htonl(INADDR_LOOPBACK);
    gsize header = packet_getHeaderSize(packet);
    gsize payload = packet_getPayloadSize(packet);
    PacketDeliveryStatusFlags status = packet_getDeliveryStatus(packet);

    if(tracker->loginfo & LOG_INFO_FLAGS_NODE) {
        if(isLocal) {
            _tracker_updateCounters(&tracker->local.outCounters, header, payload, status);
        } else {
            _tracker_updateCounters(&tracker->remote.outCounters, header, payload, status);
        }
    }

    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            if(isLocal) {
                _tracker_updateCounters(&ss->local.outCounters, header, payload, status);
            } else {
                _tracker_updateCounters(&ss->remote.outCounters, header, payload, status);
            }
        }
    }
}

void tracker_addAllocatedBytes(Tracker* tracker, gpointer location, gsize allocatedBytes) {
    MAGIC_ASSERT(tracker);

    if(tracker->loginfo & LOG_INFO_FLAGS_RAM) {
        tracker->allocatedBytesTotal += allocatedBytes;
        tracker->allocatedBytesLastInterval += allocatedBytes;
        g_hash_table_insert(tracker->allocatedLocations, location, GSIZE_TO_POINTER(allocatedBytes));
    }
}

void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location) {
    MAGIC_ASSERT(tracker);

    if(tracker->loginfo & LOG_INFO_FLAGS_RAM) {
        gpointer value = NULL;
        gboolean exists = g_hash_table_lookup_extended(tracker->allocatedLocations, location, NULL, &value);
        if(exists) {
            gboolean b = g_hash_table_remove(tracker->allocatedLocations, location);
            utility_debugAssert(b);
            gsize allocatedBytes = GPOINTER_TO_SIZE(value);
            tracker->allocatedBytesTotal -= allocatedBytes;
            tracker->deallocatedBytesLastInterval += allocatedBytes;
        } else {
            (tracker->numFailedFrees)++;
        }
    }
}

void tracker_addSocket(Tracker* tracker, const CompatSocket* socket, ProtocolType type,
                       gsize inputBufferSize, gsize outputBufferSize) {
    MAGIC_ASSERT(tracker);
    guintptr handle = _tracker_socketHandle(socket);

    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        SocketStats* ss = _socketstats_new(handle, type, inputBufferSize, outputBufferSize);
        g_hash_table_insert(tracker->socketStats, &(ss->socket), ss);
    }
}

void tracker_updateSocketPeer(Tracker* tracker, const CompatSocket* socket, in_addr_t peerIP,
                              in_port_t peerPort) {
    MAGIC_ASSERT(tracker);
    guintptr handle = _tracker_socketHandle(socket);

    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        SocketStats* socket = g_hash_table_lookup(tracker->socketStats, &handle);
        if(socket) {
            socket->peerIP = peerIP;
            socket->peerPort = peerPort;

            GString* hostnameBuffer = g_string_new(NULL);

            if(peerIP == htonl(INADDR_LOOPBACK)) {
                g_string_printf(hostnameBuffer, "127.0.0.1");
            } else if (peerIP == htonl(INADDR_ANY)) {
                g_string_printf(hostnameBuffer, "0.0.0.0");
            } else {
                const Address* address = worker_resolveIPToAddress( peerIP);
                const gchar* hostname = address ? address_toHostName(address) : NULL;
                g_string_printf(hostnameBuffer, "%s", hostname);
            }

            /* free the old string if we already have one */
            if(socket->peerHostname) {
                g_free(socket->peerHostname);
            }

            socket->peerHostname = g_string_free(hostnameBuffer, FALSE);
        }
    }
}

void tracker_updateSocketInputBuffer(Tracker* tracker, const CompatSocket* socket,
                                     gsize inputBufferLength, gsize inputBufferSize) {
    MAGIC_ASSERT(tracker);
    guintptr handle = _tracker_socketHandle(socket);

    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            ss->inputBufferLength = inputBufferLength;
            ss->inputBufferSize = inputBufferSize;
        }
    }
}

void tracker_updateSocketOutputBuffer(Tracker* tracker, const CompatSocket* socket,
                                      gsize outputBufferLength, gsize outputBufferSize) {
    MAGIC_ASSERT(tracker);
    guintptr handle = _tracker_socketHandle(socket);

    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            ss->outputBufferLength = outputBufferLength;
            ss->outputBufferSize = outputBufferSize;
        }
    }
}

void tracker_removeSocket(Tracker* tracker, const CompatSocket* socket) {
    MAGIC_ASSERT(tracker);
    guintptr handle = _tracker_socketHandle(socket);

    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            /* remove after we log the stats we have */
            ss->removeAfterNextLog = TRUE;
        }
    }
}

static gsize _tracker_sumBytes(ByteCounter* bytes) {
    utility_debugAssert(bytes);
    return bytes->controlHeader + bytes->controlHeaderRetransmit +
            bytes->dataHeader + bytes->dataHeaderRetransmit +
            bytes->dataPayload + bytes->dataPayloadRetransmit;
}

static const gchar* _tracker_getCounterHeaderString() {
    return "packets-total,bytes-total,"
            "packets-control,bytes-control-header,"
            "packets-control-retrans,bytes-control-header-retrans,"
            "packets-data,bytes-data-header,bytes-data-payload,"
            "packets-data-retrans,bytes-data-header-retrans,bytes-data-payload-retrans";
}

static gchar* _tracker_getCounterString(Counters* c) {
    utility_debugAssert(c);

    gsize totalPackets = c->packets.control + c->packets.controlRetransmit +
            c->packets.data + c->packets.dataRetransmit;
    gsize totalBytes = _tracker_sumBytes(&c->bytes);

    GString* buffer = g_string_new(NULL);
    g_string_printf(buffer,
            "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT","
            "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT","
            "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT,
            totalPackets, totalBytes,
            c->packets.control, c->bytes.controlHeader,
            c->packets.controlRetransmit, c->bytes.controlHeaderRetransmit,
            c->packets.data, c->bytes.dataHeader, c->bytes.dataPayload,
            c->packets.dataRetransmit, c->bytes.dataHeaderRetransmit, c->bytes.dataPayloadRetransmit);
    return g_string_free(buffer, FALSE);
}

static void _tracker_logNode(Tracker* tracker, LogLevel level, CSimulationTime interval) {
    guint seconds = (guint) (interval / SIMTIME_ONE_SECOND);
    gdouble cpuutil =
        (gdouble)(((gdouble)tracker->processingTimeLastIntervalNanos) / ((gdouble)interval));
    gdouble avgdelayms = 0.0;

    if(tracker->numDelayedLastInterval > 0) {
        gdouble delayms = (gdouble) (((gdouble)tracker->delayTimeLastInterval) / ((gdouble)SIMTIME_ONE_MILLISECOND));
        avgdelayms = (gdouble) (delayms / ((gdouble) tracker->numDelayedLastInterval));
    }

    if(!tracker->didLogNodeHeader) {
        tracker->didLogNodeHeader = TRUE;
        logger_log(logger_getDefault(), level, __FILE__, __FUNCTION__, __LINE__,
                   // clang-format off (Tries to break at -'s)
                   "[shadow-heartbeat] [node-header] "
                   "interval-seconds,recv-bytes,send-bytes,cpu-percent,"
                   "delayed-count,avgdelay-milliseconds;"
                   "inbound-localhost-counters;outbound-localhost-counters;"
                   "inbound-remote-counters;outbound-remote-counters "
                   "where counters are: %s",
                   // clang-format on
                   _tracker_getCounterHeaderString());
    }

    gsize totalRecvBytes = _tracker_sumBytes(&tracker->remote.inCounters.bytes);
    gsize totalSendBytes = _tracker_sumBytes(&tracker->remote.outCounters.bytes);

    gchar* inLocal = _tracker_getCounterString(&tracker->local.inCounters);
    gchar* outLocal = _tracker_getCounterString(&tracker->local.outCounters);
    gchar* inRemote = _tracker_getCounterString(&tracker->remote.inCounters);
    gchar* outRemote = _tracker_getCounterString(&tracker->remote.outCounters);

    GString* buffer = g_string_new("[shadow-heartbeat] [node] ");

    g_string_append_printf(buffer, "%u,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%f,%"G_GSIZE_FORMAT",%f;",
            seconds, totalRecvBytes, totalSendBytes, cpuutil, tracker->numDelayedLastInterval, avgdelayms);
    g_string_append_printf(buffer, "%s;%s;%s;%s", inLocal, outLocal, inRemote, outRemote);

    logger_log(logger_getDefault(), level, __FILE__, __FUNCTION__, __LINE__,
               "%s", buffer->str);

    g_free(inLocal);
    g_free(outLocal);
    g_free(inRemote);
    g_free(outRemote);
    g_string_free(buffer, TRUE);
}

static void _tracker_logSocket(Tracker* tracker, LogLevel level, CSimulationTime interval) {
    if(!tracker->didLogSocketHeader) {
        tracker->didLogSocketHeader = TRUE;
        logger_log(logger_getDefault(), level, __FILE__, __FUNCTION__, __LINE__,
                "[shadow-heartbeat] [socket-header] descriptor-number,protocol-string,hostname:port-peer;"
                "inbuflen-bytes,inbufsize-bytes,outbuflen-bytes,outbufsize-bytes;recv-bytes,send-bytes;"
                "inbound-localhost-counters;outbound-localhost-counters;"
                "inbound-remote-counters;outbound-remote-counters|..." // for each socket
                "where counters are: %s", _tracker_getCounterHeaderString());
    }

    /* construct the log message from all sockets we have in the hash table */
    GString* msg = g_string_new("[shadow-heartbeat] [socket] ");

    SocketStats* ss = NULL;
    GHashTableIter socketIterator;
    g_hash_table_iter_init(&socketIterator, tracker->socketStats);

    /* as we iterate, keep track of sockets that we should remove. we cant remove them
     * during the iteration because it will invalidate the iterator */
    GQueue* socketsToRemove = g_queue_new();
    gint socketLogCount = 0;

    while(g_hash_table_iter_next(&socketIterator, NULL, (gpointer*)&ss)) {
        /* don't log tcp sockets that don't have peer IP/port set */
        if(!ss || (ss->type == PTCP && !ss->peerIP)) {
            continue;
        }

        gsize totalRecvBytes = _tracker_sumBytes(&ss->local.inCounters.bytes) +
                _tracker_sumBytes(&ss->remote.inCounters.bytes);
        gsize totalSendBytes = _tracker_sumBytes(&ss->local.outCounters.bytes) +
                _tracker_sumBytes(&ss->remote.outCounters.bytes);

        gchar* inLocal = _tracker_getCounterString(&ss->local.inCounters);
        gchar* outLocal = _tracker_getCounterString(&ss->local.outCounters);
        gchar* inRemote = _tracker_getCounterString(&ss->remote.inCounters);
        gchar* outRemote = _tracker_getCounterString(&ss->remote.outCounters);

        /* print the node separator between node logs */
        if(socketLogCount > 0) {
            g_string_append_printf(msg, "|");
        }

        socketLogCount++;
        g_string_append_printf(msg, "%ld,%s,%s:%u;"
                "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT";"
                "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT";"
                "%s;%s;%s;%s",
                ss->socket, /*inet_ntoa((struct in_addr){socket->peerIP})*/
                ss->type == PTCP ? "TCP" : ss->type == PUDP ? "UDP" : "UNKNOWN",
                ss->peerHostname, ss->peerPort,
                ss->inputBufferLength, ss->inputBufferSize,
                ss->outputBufferLength, ss->outputBufferSize,
                totalRecvBytes, totalSendBytes,
                inLocal, outLocal, inRemote, outRemote);

        g_free(inLocal);
        g_free(outLocal);
        g_free(inRemote);
        g_free(outRemote);

        /* check if we should remove the socket after iterating */
        if(ss->removeAfterNextLog) {
            g_queue_push_tail(socketsToRemove, GINT_TO_POINTER(ss->socket));
        }
    }

    if(socketLogCount > 0) {
        logger_log(logger_getDefault(), level, __FILE__, __FUNCTION__, __LINE__, "%s", msg->str);
    }

    /* free all the tracker instances of the sockets that were closed, now that we logged the info */
    while (!g_queue_is_empty(socketsToRemove)) {
        guintptr socket = (guintptr)g_queue_pop_head(socketsToRemove);
        g_hash_table_remove(tracker->socketStats, &socket);
    }
    g_queue_free(socketsToRemove);

    g_string_free(msg, TRUE);
}

static void _tracker_logRAM(Tracker* tracker, LogLevel level, CSimulationTime interval) {
    guint seconds = (guint) (interval / SIMTIME_ONE_SECOND);
    guint numptrs = g_hash_table_size(tracker->allocatedLocations);

    if(!tracker->didLogRAMHeader) {
        tracker->didLogRAMHeader = TRUE;
        logger_log(logger_getDefault(), level, __FILE__, __FUNCTION__, __LINE__,
                "[shadow-heartbeat] [ram-header] interval-seconds,alloc-bytes,dealloc-bytes,total-bytes,pointers-count,failfree-count");
    }

    logger_log(logger_getDefault(), level, __FILE__, __FUNCTION__, __LINE__,
        "[shadow-heartbeat] [ram] %u,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%u,%u",
        seconds, tracker->allocatedBytesLastInterval, tracker->deallocatedBytesLastInterval,
        tracker->allocatedBytesTotal, numptrs, tracker->numFailedFrees);
}

void tracker_heartbeat(Tracker* tracker, const Host* host) {
    MAGIC_ASSERT(tracker);

    /* check to see if node info is being logged */
    if(tracker->loginfo & LOG_INFO_FLAGS_NODE) {
        _tracker_logNode(tracker, tracker->loglevel, tracker->interval);
    }

    /* check to see if socket buffer info is being logged */
    if(tracker->loginfo & LOG_INFO_FLAGS_SOCKET) {
        _tracker_logSocket(tracker, tracker->loglevel, tracker->interval);
    }

    /* check to see if ram info is being logged */
    if(tracker->loginfo & LOG_INFO_FLAGS_RAM) {
        _tracker_logRAM(tracker, tracker->loglevel, tracker->interval);
    }

    /* clear interval stats */
    tracker->processingTimeLastIntervalNanos = 0;
    tracker->delayTimeLastInterval = 0;
    tracker->numDelayedLastInterval = 0;
    tracker->allocatedBytesLastInterval = 0;
    tracker->deallocatedBytesLastInterval = 0;

    /* clear the counters */
    memset(&tracker->local, 0, sizeof(IFaceCounters));
    memset(&tracker->remote, 0, sizeof(IFaceCounters));

    SocketStats* ss = NULL;
    GHashTableIter socketIterator;
    g_hash_table_iter_init(&socketIterator, tracker->socketStats);
    while (g_hash_table_iter_next(&socketIterator, NULL, (gpointer*)&ss)) {
        if(ss) {
            memset(&ss->local, 0, sizeof(IFaceCounters));
            memset(&ss->remote, 0, sizeof(IFaceCounters));
        }
    }

    /* schedule the next heartbeat */
    tracker->lastHeartbeat = worker_getCurrentEmulatedTime();
    TaskRef* heartbeatTask =
        taskref_new_bound(host_getID(host), tracker_heartbeatTask, tracker, NULL, NULL, NULL);
    host_scheduleTaskWithDelay(host, heartbeatTask, tracker->interval);
    taskref_drop(heartbeatTask);
}
