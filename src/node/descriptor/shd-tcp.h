/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_H_
#define SHD_TCP_H_

#include "shadow.h"

typedef struct _TCP TCP;

TCP* tcp_new(gint handle, guint receiveBufferSize, guint sendBufferSize);
gint tcp_getConnectError(TCP* tcp);
void tcp_enterServerMode(TCP* tcp, gint backlog);
gint tcp_acceptServerPeer(TCP* tcp, in_addr_t* ip, in_port_t* port, gint* acceptedHandle);
void tcp_closeTimerExpired(TCP* tcp);

#endif /* SHD_TCP_H_ */
