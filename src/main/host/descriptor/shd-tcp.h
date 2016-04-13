/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_H_
#define SHD_TCP_H_

#include "shadow.h"

typedef struct _TCP TCP;

typedef enum TCPProcessFlags TCPProcessFlags;
enum TCPProcessFlags {
    TCP_PF_NONE = 0,
    TCP_PF_PROCESSED = 1 << 0,
    TCP_PF_DATA_ACKED = 1 << 1,
    TCP_PF_DATA_SACKED = 1 << 2,
    TCP_PF_DATA_LOST = 1 << 3,
};

TCP* tcp_new(gint handle, guint receiveBufferSize, guint sendBufferSize);
gint tcp_getConnectError(TCP* tcp);
void tcp_getInfo(TCP* tcp, struct tcp_info *tcpinfo);
void tcp_enterServerMode(TCP* tcp, gint backlog);
gint tcp_acceptServerPeer(TCP* tcp, in_addr_t* ip, in_port_t* port, gint* acceptedHandle);

void tcp_clearAllChildrenIfServer(TCP* tcp);

#endif /* SHD_TCP_H_ */
