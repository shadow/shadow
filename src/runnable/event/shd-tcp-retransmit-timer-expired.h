/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_RETRANSMIT_TIMER_EXPIRED_H_
#define SHD_TCP_RETRANSMIT_TIMER_EXPIRED_H_

#include "shadow.h"

typedef struct _TCPRetransmitTimerExpiredEvent TCPRetransmitTimerExpiredEvent;

TCPRetransmitTimerExpiredEvent* tcpretransmittimerexpired_new(TCP* tcp);
void tcpretransmittimerexpired_run(TCPRetransmitTimerExpiredEvent* event, Host* node);
void tcpretransmittimerexpired_free(TCPRetransmitTimerExpiredEvent* event);

#endif /* SHD_TCP_RETRANSMIT_TIMER_EXPIRED_H_ */
