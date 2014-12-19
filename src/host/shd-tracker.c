/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef enum _TrackerFlags TrackerFlags;
enum _TrackerFlags {
    TRACKER_FLAGS_NONE = 0,
    TRACKER_FLAGS_NODE = 1<<0,
    TRACKER_FLAGS_SOCKET = 1<<1,
    TRACKER_FLAGS_RAM = 1<<2,
};

typedef struct {
    gsize controlHeader;
    gsize payloadHeader;
    gsize payload;
    gsize retransmitHeader;
    gsize retransmitPayload;
} ByteCounter;

typedef struct {
    gsize control;
    gsize payload;
    gsize retransmit;
} PacketCounter;

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
    SimulationTime interval;
    GLogLevelFlags loglevel;
    TrackerFlags privateFlags;

    /* simulation global flags, only to be used if we have no personal flags set */
    TrackerFlags globalFlags;

    gboolean didLogNodeHeader;
    gboolean didLogRAMHeader;
    gboolean didLogSocketHeader;

    SimulationTime processingTimeTotal;
    SimulationTime processingTimeLastInterval;

    gsize numDelayedTotal;
    SimulationTime delayTimeTotal;
    gsize numDelayedLastInterval;
    SimulationTime delayTimeLastInterval;

    IFaceCounters local;
    IFaceCounters remote;

    GHashTable* allocatedLocations;
    gsize allocatedBytesTotal;
    gsize allocatedBytesLastInterval;
    gsize deallocatedBytesLastInterval;
    guint numFailedFrees;

    GHashTable* socketStats;

    SimulationTime lastHeartbeat;

    MAGIC_DECLARE;
};

typedef struct _SocketStats SocketStats;
struct _SocketStats {
    gint handle;
    enum ProtocolType type;

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

static SocketStats* _socketstats_new(gint handle, enum ProtocolType type,
        gsize inputBufferSize, gsize outputBufferSize) {
    SocketStats* ss = g_new0(SocketStats, 1);

    ss->handle = handle;
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

static TrackerFlags _tracker_parseFlagString(gchar* flagString) {
    TrackerFlags flags = TRACKER_FLAGS_NONE;
    if(flagString) {
        /* info string can either be comma or space separated */
        gchar** parts = g_strsplit_set(flagString, " ,", -1);
        for(gint idx = 0; parts[idx]; idx++) {
            if(!g_ascii_strcasecmp(parts[idx], "node")) {
                flags |= TRACKER_FLAGS_NODE;
            } else if(!g_ascii_strcasecmp(parts[idx], "socket")) {
                flags |= TRACKER_FLAGS_SOCKET;
            } else if(!g_ascii_strcasecmp(parts[idx], "ram")) {
                flags |= TRACKER_FLAGS_RAM;
            } else {
                warning("Did not recognize log info '%s', possible choices are 'node','socket','ram'.", parts[idx]);
            }
        }
        g_strfreev(parts);
    }

    return flags;
}

static TrackerFlags _tracker_parseGlobalFlags() {
    TrackerFlags flags = TRACKER_FLAGS_NONE;
    Configuration* c = worker_getConfig();
    flags = _tracker_parseFlagString(c->heartbeatLogInfo);
    return flags;
}

static GLogLevelFlags _tracker_getLogLevel(Tracker* tracker) {
    /* prefer our level over the global config */
    GLogLevelFlags level = tracker->loglevel;
    if(!level) {
        Configuration* c = worker_getConfig();
        level = configuration_getHeartbeatLogLevel(c);
    }
    return level;
}

static SimulationTime _tracker_getLogInterval(Tracker* tracker) {
    /* prefer our interval over the global config */
    SimulationTime interval = tracker->interval;
    if(!interval) {
        Configuration* c = worker_getConfig();
        interval = configuration_getHearbeatInterval(c);
    }
    return interval;
}

static TrackerFlags _tracker_getFlags(Tracker* tracker) {
    /* prefer our log info over the global config */
    return tracker->privateFlags ? tracker->privateFlags : tracker->globalFlags;
}

Tracker* tracker_new(SimulationTime interval, GLogLevelFlags loglevel, gchar* flagString) {
    Tracker* tracker = g_new0(Tracker, 1);
    MAGIC_INIT(tracker);

    tracker->interval = interval;
    tracker->loglevel = loglevel;
    tracker->privateFlags = _tracker_parseFlagString(flagString);
    tracker->globalFlags = _tracker_parseGlobalFlags();

    tracker->allocatedLocations = g_hash_table_new(g_direct_hash, g_direct_equal);
    tracker->socketStats = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)_socketstats_free);

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

void tracker_addProcessingTime(Tracker* tracker, SimulationTime processingTime) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_NODE) {
        tracker->processingTimeTotal += processingTime;
        tracker->processingTimeLastInterval += processingTime;
    }
}

void tracker_addVirtualProcessingDelay(Tracker* tracker, SimulationTime delay) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_NODE) {
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
        c->packets.payload++;
        c->bytes.payloadHeader += header;
        c->bytes.payload += payload;
    } else {
        c->packets.control++;
        c->bytes.controlHeader += header;
    }

    if(status & PDS_SND_TCP_RETRANSMITTED) {
        c->packets.retransmit++;
        c->bytes.retransmitHeader += header;
        c->bytes.retransmitPayload += payload;
    }
}

void tracker_addInputBytes(Tracker* tracker, Packet* packet, gint handle) {
    MAGIC_ASSERT(tracker);

    TrackerFlags flags = _tracker_getFlags(tracker);
    if(!(flags & TRACKER_FLAGS_NODE) && !(flags & TRACKER_FLAGS_SOCKET)) {
        return;
    }

    gboolean isLocal = packet_getDestinationIP(packet) == htonl(INADDR_LOOPBACK);
    gsize header = (gsize)packet_getHeaderSize(packet);
    gsize payload = (gsize)packet_getPayloadLength(packet);
    PacketDeliveryStatusFlags status = packet_getDeliveryStatus(packet);

    if(flags & TRACKER_FLAGS_NODE) {
        if(isLocal) {
            _tracker_updateCounters(&tracker->local.inCounters, header, payload, status);
        } else {
            _tracker_updateCounters(&tracker->remote.inCounters, header, payload, status);
        }
    }

    if(flags & TRACKER_FLAGS_SOCKET) {
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

void tracker_addOutputBytes(Tracker* tracker, Packet* packet, gint handle) {
    MAGIC_ASSERT(tracker);

    TrackerFlags flags = _tracker_getFlags(tracker);
    if(!(flags & TRACKER_FLAGS_NODE) && !(flags & TRACKER_FLAGS_SOCKET)) {
        return;
    }

    gboolean isLocal = packet_getSourceIP(packet) == htonl(INADDR_LOOPBACK);
    gsize header = (gsize)packet_getHeaderSize(packet);
    gsize payload = (gsize)packet_getPayloadLength(packet);
    PacketDeliveryStatusFlags status = packet_getDeliveryStatus(packet);

    if(flags & TRACKER_FLAGS_NODE) {
        if(isLocal) {
            _tracker_updateCounters(&tracker->local.outCounters, header, payload, status);
        } else {
            _tracker_updateCounters(&tracker->remote.outCounters, header, payload, status);
        }
    }

    if(flags & TRACKER_FLAGS_SOCKET) {
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

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_RAM) {
        tracker->allocatedBytesTotal += allocatedBytes;
        tracker->allocatedBytesLastInterval += allocatedBytes;
        g_hash_table_insert(tracker->allocatedLocations, location, GSIZE_TO_POINTER(allocatedBytes));
    }
}

void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_RAM) {
        gpointer value = NULL;
        gboolean exists = g_hash_table_lookup_extended(tracker->allocatedLocations, location, NULL, &value);
        if(exists) {
            utility_assert(g_hash_table_remove(tracker->allocatedLocations, location));
            gsize allocatedBytes = GPOINTER_TO_SIZE(value);
            tracker->allocatedBytesTotal -= allocatedBytes;
            tracker->deallocatedBytesLastInterval += allocatedBytes;
        } else {
            (tracker->numFailedFrees)++;
        }
    }
}

void tracker_addSocket(Tracker* tracker, gint handle, enum ProtocolType type, gsize inputBufferSize, gsize outputBufferSize) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
        SocketStats* ss = _socketstats_new(handle, type, inputBufferSize, outputBufferSize);
        g_hash_table_insert(tracker->socketStats, &(ss->handle), ss);
    }
}

void tracker_updateSocketPeer(Tracker* tracker, gint handle, in_addr_t peerIP, in_port_t peerPort) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
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
                g_string_printf(hostnameBuffer, "%s", dns_resolveIPToName(worker_getDNS(), peerIP));
            }

            /* free the old string if we already have one */
            if(socket->peerHostname) {
                g_free(socket->peerHostname);
            }

            socket->peerHostname = g_string_free(hostnameBuffer, FALSE);
        }
    }
}

void tracker_updateSocketInputBuffer(Tracker* tracker, gint handle, gsize inputBufferLength, gsize inputBufferSize) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            ss->inputBufferLength = inputBufferLength;
            ss->inputBufferSize = inputBufferSize;
        }
    }
}

void tracker_updateSocketOutputBuffer(Tracker* tracker, gint handle, gsize outputBufferLength, gsize outputBufferSize) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            ss->outputBufferLength = outputBufferLength;
            ss->outputBufferSize = outputBufferSize;
        }
    }
}

void tracker_removeSocket(Tracker* tracker, gint handle) {
    MAGIC_ASSERT(tracker);

    if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
        SocketStats* ss = g_hash_table_lookup(tracker->socketStats, &handle);
        if(ss) {
            /* remove after we log the stats we have */
            ss->removeAfterNextLog = TRUE;
        }
    }
}

static gchar* _tracker_getCounterString(Counters* c) {
    g_assert(c);

    gsize totalPackets = c->packets.control + c->packets.payload;
    gsize totalHeaderBytes = c->bytes.controlHeader + c->bytes.payloadHeader;
    gsize totalBytes = totalHeaderBytes + c->bytes.payload;

    GString* buffer = g_string_new(NULL);
    g_string_printf(buffer,
            "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT","
            "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT","
            "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT,
            totalPackets, totalBytes, c->bytes.payload, totalHeaderBytes,
            c->packets.payload, c->bytes.payloadHeader,
            c->packets.control, c->bytes.controlHeader,
            c->packets.retransmit, c->bytes.retransmitHeader, c->bytes.retransmitPayload);
    return g_string_free(buffer, FALSE);
}

static void _tracker_logNode(Tracker* tracker, GLogLevelFlags level, SimulationTime interval) {
    guint seconds = (guint) (interval / SIMTIME_ONE_SECOND);
    gdouble cpuutil = (gdouble)(((gdouble)tracker->processingTimeLastInterval) / ((gdouble)interval));
    gdouble avgdelayms = 0.0;

    if(tracker->numDelayedLastInterval > 0) {
        gdouble delayms = (gdouble) (((gdouble)tracker->delayTimeLastInterval) / ((gdouble)SIMTIME_ONE_MILLISECOND));
        avgdelayms = (gdouble) (delayms / ((gdouble) tracker->numDelayedLastInterval));
    }

    gsize totalRecvBytes = tracker->remote.inCounters.bytes.controlHeader +
            tracker->remote.inCounters.bytes.payloadHeader +
            tracker->remote.inCounters.bytes.payload;

    gsize totalSendBytes = tracker->remote.outCounters.bytes.controlHeader +
            tracker->remote.outCounters.bytes.payloadHeader +
            tracker->remote.outCounters.bytes.payload;

    if(!tracker->didLogNodeHeader) {
        tracker->didLogNodeHeader = TRUE;
        logging_log(G_LOG_DOMAIN, level, __FUNCTION__, "[shadow-heartbeat] [node-header] "
                "interval-seconds,recv-bytes,send-bytes,cpu-percent,delayed-count,avgdelay-milliseconds;"
                "inbound-localhost-counters;outbound-localhost-counters;"
                "inbound-remote-counters;outbound-remote-counters "
                "where counters are: total-packets,total-bytes,payload-bytes,header-bytes,"
                "payload-packets,payload-header-bytes,control-packets,control-header-bytes,"
                "retrans-packets,retrans-header-bytes,retrans-payload-bytes"
        );
    }

    gchar* inLocal = _tracker_getCounterString(&tracker->local.inCounters);
    gchar* outLocal = _tracker_getCounterString(&tracker->local.outCounters);
    gchar* inRemote = _tracker_getCounterString(&tracker->remote.inCounters);
    gchar* outRemote = _tracker_getCounterString(&tracker->remote.outCounters);

    GString* buffer = g_string_new("[shadow-heartbeat] [node] ");

    g_string_append_printf(buffer, "%u,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%f,%"G_GSIZE_FORMAT",%f;",
            seconds, totalRecvBytes, totalSendBytes, cpuutil, tracker->numDelayedLastInterval, avgdelayms);
    g_string_append_printf(buffer, "%s;%s;%s;%s", inLocal, outLocal, inRemote, outRemote);

    logging_log(G_LOG_DOMAIN, level, __FUNCTION__, "%s", buffer->str);

    g_free(inLocal);
    g_free(outLocal);
    g_free(inRemote);
    g_free(outRemote);
    g_string_free(buffer, TRUE);
}

static void _tracker_logSocket(Tracker* tracker, GLogLevelFlags level, SimulationTime interval) {
    if(!tracker->didLogSocketHeader) {
        tracker->didLogSocketHeader = TRUE;
        logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
                "[shadow-heartbeat] [socket-header] descriptor-number,protocol-string,hostname:port-peer,inbuflen-bytes,inbufsize-bytes,outbuflen-bytes,outbufsize-bytes,recv-bytes,send-bytes;...");
    }

    /* construct the log message from all sockets we have in the hash table */
    GString* msg = g_string_new("[shadow-heartbeat] [socket] ");

    SocketStats* ss = NULL;
    GHashTableIter socketIterator;
    g_hash_table_iter_init(&socketIterator, tracker->socketStats);

    /* as we iterate, keep track of sockets that we should remove. we cant remove them
     * during the iteration because it will invalidate the iterator */
    GQueue* handlesToRemove = g_queue_new();
    gint socketLogCount = 0;

    while(g_hash_table_iter_next(&socketIterator, NULL, (gpointer*)&ss)) {
        /* don't log tcp sockets that don't have peer IP/port set */
        if(!ss || (ss->type == PTCP && !ss->peerIP)) {
            continue;
        }

        gsize totalRecvBytes = ss->local.inCounters.bytes.controlHeader +
                ss->local.inCounters.bytes.payloadHeader +
                ss->local.inCounters.bytes.payload +
                ss->remote.inCounters.bytes.controlHeader +
                ss->remote.inCounters.bytes.payloadHeader +
                ss->remote.inCounters.bytes.payload;

        gsize totalSendBytes = ss->local.outCounters.bytes.controlHeader +
                ss->local.outCounters.bytes.payloadHeader +
                ss->local.outCounters.bytes.payload +
                ss->remote.outCounters.bytes.controlHeader +
                ss->remote.outCounters.bytes.payloadHeader +
                ss->remote.outCounters.bytes.payload;

        socketLogCount++;
        g_string_append_printf(msg, "%d,%s,%s:%u,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT";",
                ss->handle, /*inet_ntoa((struct in_addr){socket->peerIP})*/
                ss->type == PTCP ? "TCP" : ss->type == PUDP ? "UDP" :
                    ss->type == PLOCAL ? "LOCAL" : "UNKNOWN",
                ss->peerHostname, ss->peerPort,
                ss->inputBufferLength, ss->inputBufferSize,
                ss->outputBufferLength, ss->outputBufferSize,
                totalRecvBytes, totalSendBytes);

        /* check if we should remove the socket after iterating */
        if(ss->removeAfterNextLog) {
            g_queue_push_tail(handlesToRemove, GINT_TO_POINTER(ss->handle));
        }
    }

    if(socketLogCount > 0) {
        logging_log(G_LOG_DOMAIN, level, __FUNCTION__, "%s", msg->str);
    }

    /* free all the tracker instances of the sockets that were closed, now that we logged the info */
    while(!g_queue_is_empty(handlesToRemove)) {
        gint handle = GPOINTER_TO_INT(g_queue_pop_head(handlesToRemove));
        g_hash_table_remove(tracker->socketStats, &handle);
    }
    g_queue_free(handlesToRemove);

    g_string_free(msg, TRUE);
}

static void _tracker_logRAM(Tracker* tracker, GLogLevelFlags level, SimulationTime interval) {
    guint seconds = (guint) (interval / SIMTIME_ONE_SECOND);
    guint numptrs = g_hash_table_size(tracker->allocatedLocations);

    if(!tracker->didLogRAMHeader) {
        tracker->didLogRAMHeader = TRUE;
        logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
                "[shadow-heartbeat] [ram-header] interval-seconds,alloc-bytes,dealloc-bytes,total-bytes,pointers-count,failfree-count");
    }

    logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
        "[shadow-heartbeat] [ram] %u,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%u,%u",
        seconds, tracker->allocatedBytesLastInterval, tracker->deallocatedBytesLastInterval,
        tracker->allocatedBytesTotal, numptrs, tracker->numFailedFrees);
}

static void _tracker_clearCounters(Counters* counter) {
    if(!counter) {
        return;
    }
    counter->bytes.controlHeader = 0;
    counter->bytes.payloadHeader = 0;
    counter->bytes.payload = 0;
    counter->bytes.retransmitHeader = 0;
    counter->bytes.retransmitPayload = 0;
    counter->packets.control = 0;
    counter->packets.payload = 0;
    counter->packets.retransmit = 0;
}

void tracker_heartbeat(Tracker* tracker) {
    MAGIC_ASSERT(tracker);

    TrackerFlags flags = _tracker_getFlags(tracker);
    GLogLevelFlags level = _tracker_getLogLevel(tracker);
    SimulationTime interval = _tracker_getLogInterval(tracker);

    /* check to see if node info is being logged */
    if(flags & TRACKER_FLAGS_NODE) {
        _tracker_logNode(tracker, level, interval);
    }

    /* check to see if socket buffer info is being logged */
    if(flags & TRACKER_FLAGS_SOCKET) {
        _tracker_logSocket(tracker, level, interval);
    }

    /* check to see if ram info is being logged */
    if(flags & TRACKER_FLAGS_RAM) {
        _tracker_logRAM(tracker, level, interval);
    }

    /* make sure we have the latest global configured flags */
    tracker->globalFlags = _tracker_parseGlobalFlags();

    /* clear interval stats */
    tracker->processingTimeLastInterval = 0;
    tracker->delayTimeLastInterval = 0;
    tracker->numDelayedLastInterval = 0;
    tracker->allocatedBytesLastInterval = 0;
    tracker->deallocatedBytesLastInterval = 0;

    _tracker_clearCounters(&tracker->local.inCounters);
    _tracker_clearCounters(&tracker->local.outCounters);
    _tracker_clearCounters(&tracker->remote.inCounters);
    _tracker_clearCounters(&tracker->remote.outCounters);

    SocketStats* ss = NULL;
    GHashTableIter socketIterator;
    g_hash_table_iter_init(&socketIterator, tracker->socketStats);
    while (g_hash_table_iter_next(&socketIterator, NULL, (gpointer*)&ss)) {
        if(ss) {
            _tracker_clearCounters(&ss->local.inCounters);
            _tracker_clearCounters(&ss->local.outCounters);
            _tracker_clearCounters(&ss->remote.inCounters);
            _tracker_clearCounters(&ss->remote.outCounters);
        }
    }

    /* schedule the next heartbeat */
    tracker->lastHeartbeat = worker_getCurrentTime();
    HeartbeatEvent* heartbeat = heartbeat_new(tracker);
    worker_scheduleEvent((Event*)heartbeat, interval, 0);
}
