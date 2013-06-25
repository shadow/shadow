/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_CLOSE_TIMER_EXPIRED_H_
#define SHD_TCP_CLOSE_TIMER_EXPIRED_H_

#include "shadow.h"

typedef struct _TCPCloseTimerExpiredEvent TCPCloseTimerExpiredEvent;

TCPCloseTimerExpiredEvent* tcpclosetimerexpired_new(TCP* tcp);
void tcpclosetimerexpired_run(TCPCloseTimerExpiredEvent* event, Node* node);
void tcpclosetimerexpired_free(TCPCloseTimerExpiredEvent* event);

#endif /* SHD_TCP_CLOSE_TIMER_EXPIRED_H_ */
