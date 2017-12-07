/*
 * The Shadow Simulator
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include <stdio.h>

struct _Reno {
    TCPCongestion super;
    gdouble window;
    MAGIC_DECLARE;
};

void reno_congestionAvoidance(Reno* reno, gint inFlight, gint packetsAcked, gint ack) {
    MAGIC_ASSERT(reno);
    TCPCongestion* congestion = (TCPCongestion*)reno;

    if (congestion->window != (guint32)floor(reno->window)) {
       reno->window = (gdouble)congestion->window;
    }

    if(congestion->state == TCP_CCS_SLOWSTART) {
        congestion->window += ((guint32)packetsAcked);

        if(congestion->threshold != 0 && congestion->window >= congestion->threshold) {
            congestion->state = TCP_CCS_AVOIDANCE;
            reno->window = congestion->window;
        }
    } else {
        gdouble n = ((gdouble) packetsAcked);
        gdouble increment = n * n / ((gdouble) congestion->window);
        reno->window += increment;
        congestion->window = (guint32)(floor(reno->window));
    }
}

guint reno_packetLoss(Reno* reno) {
    MAGIC_ASSERT(reno);
    TCPCongestion* congestion = (TCPCongestion*)reno;
    reno->window = congestion->window;

    reno->window = (guint32) ceil((gdouble)reno->window / (gdouble)2);

    reno->window = MAX(reno->window, TCP_MIN_CWND);

    congestion->window = (guint32)(ceil(reno->window));

    return 0;
}

static void _reno_free(Reno* reno) {
    MAGIC_ASSERT(reno);
    MAGIC_CLEAR(reno);
    g_free(reno);
}

TCPCongestionFunctionTable renoFunctions = {
    (TCPCongestionAvoidanceFunc) reno_congestionAvoidance,
    (TCPCongestionPacketLossFunc) reno_packetLoss,
    (TCPCongestionFreeFunc) _reno_free,
    MAGIC_VALUE
};

Reno* reno_new(gint window, gint threshold) {
    Reno *reno = g_new0(Reno, 1);
    MAGIC_INIT(reno);

    tcpCongestion_init(&(reno->super), &renoFunctions, TCP_CC_RENO, window, threshold);

    reno->window = window;
    reno->super.fastRetransmit = TCP_FR_RENO;

    return reno;
}
