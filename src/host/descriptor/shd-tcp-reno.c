/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Reno {
    TCPCongestion super;
    gboolean isSlowStart;
    gdouble window;
    MAGIC_DECLARE;
};

void reno_congestionAvoidance(Reno* reno, gint inFlight, gint packetsAcked, gint ack) {
	MAGIC_ASSERT(reno);
    TCPCongestion* congestion = (TCPCongestion*)reno;

    if(reno->isSlowStart) {
        /* threshold not set => no timeout yet => slow start phase 1
         *  i.e. multiplicative increase until retransmit event (which sets threshold)
         * threshold set => timeout => slow start phase 2
         *  i.e. multiplicative increase until threshold */
        congestion->window += ((guint32)packetsAcked);
        if(congestion->threshold != 0 && congestion->window >= congestion->threshold) {
            reno->isSlowStart = FALSE;
        }
    } else {
        /* slow start is over
         * simple additive increase part of Reno */
        gdouble n = ((gdouble) packetsAcked);
        gdouble increment = n * n / ((gdouble) congestion->window);
        reno->window += increment;
        congestion->window = (guint32)(floor(reno->window));
    }
}

void reno_packetLoss(Reno* reno) {
	MAGIC_ASSERT(reno);
    TCPCongestion* congestion = (TCPCongestion*)reno;

    /* a packet was "dropped" - this is basically a negative ack.
     * TCP-Reno-like fast retransmit, i.e. multiplicative decrease. */
    reno->window = (guint32) ceil((gdouble)reno->window / (gdouble)2);

    if(reno->isSlowStart && congestion->threshold == 0) {
        congestion->threshold = reno->window;
    }

	/* unlike the send and receive/advertised windows, our cong window should never be 0
	 *
	 * from https://tools.ietf.org/html/rfc5681 [page 6]:
	 *
	 * "Implementation Note: Since integer arithmetic is usually used in TCP
   	 *  implementations, the formula given in equation (3) can fail to
   	 *  increase window when the congestion window is larger than SMSS*SMSS.
   	 *  If the above formula yields 0, the result SHOULD be rounded up to 1 byte."
	 */
	if(reno->window == 0) {
		reno->window = 1;
	}

    congestion->window = reno->window;
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
    reno->isSlowStart = TRUE;
    reno->super.fastRetransmit = FALSE;

    return reno;
}
