/*
 * The Shadow Simulator
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_CONGESTION_H
#define SHD_TCP_CONGESTION_H

#include "shadow.h"

typedef enum _TCPCongestionType TCPCongestionType;
enum _TCPCongestionType {
    TCP_CC_UNKNOWN, TCP_CC_AIMD, TCP_CC_RENO, TCP_CC_CUBIC,
};

typedef enum _TCPFastRetransmitType TCPFastRetransmitType;
enum _TCPFastRetransmitType {
    TCP_FR_NONE, TCP_FR_RENO, TCP_FR_SACK,
};

typedef enum _TCPCongestionState TCPCongestionState;
enum _TCPCongestionState {
    TCP_CCS_SLOWSTART, TCP_CCS_AVOIDANCE, 
    TCP_CCS_FASTRETRANSMIT, TCP_CCS_FASTRECOVERY,
};

typedef struct _TCPCongestion TCPCongestion;
typedef struct _TCPCongestionFunctionTable TCPCongestionFunctionTable;

typedef void (*TCPCongestionAvoidanceFunc)(TCPCongestion* congestion, gint inFlight, gint packetsAcked, gint ack);
typedef guint (*TCPCongestionPacketLossFunc)(TCPCongestion* congestion);
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
    TCPCongestionState state;
    /* congestion window (cwnd) */
    gint window;
    /* slow start threshold for window (ssthresh) */
    gint threshold;
    /* calculate RTT from header timestamps (srrt) */
    gint rttSmoothed;
    /* variance of the calculated RTT (rttvar) */
    gint rttVariance;
    /* type of fast retransmit being performed (default=None) */
    TCPFastRetransmitType fastRetransmit;
    MAGIC_DECLARE;
};

void tcpCongestion_init(TCPCongestion* congestion, TCPCongestionFunctionTable* funcTable,TCPCongestionType type,  gint window, gint threshold);
void tcpCongestion_avoidance(TCPCongestion* congestion, gint inFlight, gint packetsAcked, gint ack);
guint tcpCongestion_packetLoss(TCPCongestion* congestion);
void tcpCongestion_free(TCPCongestion* congestion);

TCPCongestionType tcpCongestion_getType(gchar* type);

#endif
