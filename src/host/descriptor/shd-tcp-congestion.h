/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_CONGESTION_H
#define SHD_TCP_CONGESTION_H

#include "shadow.h"

typedef enum _TCPCongestionType TCPCongestionType;
enum _TCPCongestionType {
    TCP_CC_UNKNOWN, TCP_CC_AIMD, TCP_CC_RENO, TCP_CC_CUBIC,
};

typedef struct _TCPCongestion TCPCongestion;
typedef struct _TCPCongestionFunctionTable TCPCongestionFunctionTable;

typedef void (*TCPCongestionAvoidanceFunc)(TCPCongestion* congestion, gint inFlight, gint packetsAcked, gint ack);
typedef void (*TCPCongestionPacketLossFunc)(TCPCongestion* congestion);
typedef void (*TCPCongestionFreeFunc)(TCPCongestion* congsetion);

struct _TCPCongestionFunctionTable {
    TCPCongestionAvoidanceFunc avoidance;
    TCPCongestionPacketLossFunc packetLoss;
    TCPCongestionFreeFunc free;
    MAGIC_DECLARE;
};

struct _TCPCongestion {
    TCPCongestionFunctionTable* funcTable;
    TCPCongestionType type;
    /* congestion window (cwnd) */
    gint window;
    /* slow start threshold for window (ssthresh) */
    gint threshold;
    /* calculate RTT from header timestamps (srrt) */
    gint rttSmoothed;
    /* variance of the calculated RTT (rttvar) */
    gint rttVariance;
    /* whether or not we're performing fast retransmit */
    gboolean fastRetransmit;
    MAGIC_DECLARE;
};

void tcpCongestion_init(TCPCongestion* congestion, TCPCongestionFunctionTable* funcTable,TCPCongestionType type,  gint window, gint threshold);
void tcpCongestion_avoidance(TCPCongestion* congestion, gint inFlight, gint packetsAcked, gint ack);
void tcpCongestion_packetLoss(TCPCongestion* congestion);
void tcpCongestion_free(TCPCongestion* congestion);

TCPCongestionType tcpCongestion_getType(gchar* type);

#endif
