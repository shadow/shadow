/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _AIMD {
    TCPCongestion super;
    gboolean isSlowStart;
    MAGIC_DECLARE;
};

void aimd_congestionAvoidance(AIMD* aimd, gint inFlight, gint packetsAcked, gint ack) {
	MAGIC_ASSERT(aimd);
    TCPCongestion* congestion = (TCPCongestion*)aimd;

    if(aimd->isSlowStart) {
        /* threshold not set => no timeout yet => slow start phase 1
         *  i.e. multiplicative increase until retransmit event (which sets threshold)
         * threshold set => timeout => slow start phase 2
         *  i.e. multiplicative increase until threshold */
        congestion->window += ((guint32)packetsAcked);
        if(congestion->threshold != 0 && congestion->window >= congestion->threshold) {
            aimd->isSlowStart = FALSE;
        }
    } else {
        /* slow start is over
         * simple additive increase part of AIMD */
        gdouble n = ((gdouble) packetsAcked);
        gdouble increment = n * n / ((gdouble) congestion->window);
        congestion->window += (guint32)(ceil(increment));
    }
}

void aimd_packetLoss(AIMD* aimd) {
	MAGIC_ASSERT(aimd);
    TCPCongestion* congestion = (TCPCongestion*)aimd;

    /* a packet was "dropped" - this is basically a negative ack.
     * TCP-Reno-like fast retransmit, i.e. multiplicative decrease. */
    congestion->window = (guint32) ceil((gdouble)congestion->window / (gdouble)2);

    if(aimd->isSlowStart && congestion->threshold == 0) {
        congestion->threshold = congestion->window;
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
	if(congestion->window == 0) {
		congestion->window = 1;
	}
}

static void _aimd_free(AIMD* aimd) {
	MAGIC_ASSERT(aimd);
	MAGIC_CLEAR(aimd);
	g_free(aimd);
}

TCPCongestionFunctionTable aimdFunctions = {
    (TCPCongestionAvoidanceFunc) aimd_congestionAvoidance,
    (TCPCongestionPacketLossFunc) aimd_packetLoss,
    (TCPCongestionFreeFunc) _aimd_free,
    MAGIC_VALUE
};

AIMD* aimd_new(gint window, gint threshold) {
    AIMD *aimd = g_new0(AIMD, 1);
    MAGIC_INIT(aimd);

    tcpCongestion_init(&(aimd->super), &aimdFunctions, TCP_CC_AIMD, window, threshold);

    aimd->isSlowStart = TRUE;
    aimd->super.fastRetransmit = TCP_FR_NONE;

    return aimd;
}
