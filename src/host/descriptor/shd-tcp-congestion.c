/*
 * The Shadow Simulator
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#include "shadow.h"

TCPCongestionType tcpCongestion_getType(gchar* type) {
    if(!g_ascii_strcasecmp(type, "aimd")) {
        return TCP_CC_AIMD;
    } else if(!g_ascii_strcasecmp(type, "reno")) {
        return TCP_CC_RENO;
    } else if(!g_ascii_strcasecmp(type, "cubic")) {
        return TCP_CC_CUBIC;
    }

    return TCP_CC_UNKNOWN;
}


void tcpCongestion_init(TCPCongestion* congestion, TCPCongestionFunctionTable* funcTable,TCPCongestionType type,
        gint window, gint threshold) {
    MAGIC_INIT(congestion);
    MAGIC_INIT(funcTable);
    congestion->funcTable = funcTable;
    congestion->type = type;
    congestion->window = window;
    congestion->threshold = threshold;
    congestion->state = TCP_CCS_SLOWSTART;
}

void tcpCongestion_avoidance(TCPCongestion* congestion, gint inFlight, gint packetsAcked, gint ack) {
    MAGIC_ASSERT(congestion);
    MAGIC_ASSERT(congestion->funcTable);
    congestion->funcTable->avoidance(congestion, inFlight, packetsAcked, ack);
}

guint tcpCongestion_packetLoss(TCPCongestion* congestion) {
    MAGIC_ASSERT(congestion);
    MAGIC_ASSERT(congestion->funcTable);
    return congestion->funcTable->packetLoss(congestion);
}

void tcpCongestion_free(TCPCongestion* congestion) {
    MAGIC_ASSERT(congestion);
    MAGIC_ASSERT(congestion->funcTable);
    congestion->funcTable->free(congestion);
}
