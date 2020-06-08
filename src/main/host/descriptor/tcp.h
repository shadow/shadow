/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_H_
#define SHD_TCP_H_

#include <glib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>

#include "main/routing/packet.h"

#define TCP_MIN_CWND 10

typedef struct _TCP TCP;
struct TCPCong_;

/* these were redefined in shd-tcp-retransmit-tally.h
 * if they change here, they must also change there!! (-RSW)
 */
typedef enum TCPProcessFlags TCPProcessFlags;
enum TCPProcessFlags {
    TCP_PF_NONE = 0,
    TCP_PF_PROCESSED = 1 << 0,
    TCP_PF_DATA_RECEIVED = 1 << 1,
    TCP_PF_DATA_ACKED = 1 << 2,
    TCP_PF_DATA_SACKED = 1 << 3,
    TCP_PF_DATA_LOST = 1 << 4,
    TCP_PF_RWND_UPDATED = 1 << 5,
};

typedef enum _TCPCongestionType TCPCongestionType;
enum _TCPCongestionType {
    TCP_CC_UNKNOWN, TCP_CC_AIMD, TCP_CC_RENO, TCP_CC_CUBIC,
};

TCP* tcp_new(guint receiveBufferSize, guint sendBufferSize);

// clang-format off
/* Returns a positive number to indicate that we have not yet sent a SYN
 * packet, i.e., connect() has not been called.
 *
 * Returns 0 to signal that a previous connect() attempt succeeded. A 0 return
 * code is only returned once, after which it is assumed that the successful
 * connect() has been signaled to the user.
 *
 * Otherwise returns a negative code:
 * -ECONNRESET: an established connection failed unexpectedly
 * -ENOTCONN: the connection was established, but now both reading and writing
 *            are done
 * -EISCONN: the connection is established and we already returned 0 once to
 *           indicate a successful 3-way handshake
 * -ECONNREFUSED: the 3-way handshake failed
 * -EALREADY: connect() was called and we are waiting for the 3-way handshake
 */
gint tcp_getConnectionError(TCP* tcp);
// clang-format on

void tcp_getInfo(TCP* tcp, struct tcp_info *tcpinfo);
void tcp_enterServerMode(TCP* tcp, gint backlog);
gint tcp_acceptServerPeer(TCP* tcp, in_addr_t* ip, in_port_t* port, gint* acceptedHandle);

struct TCPCong_ *tcp_cong(TCP *tcp);

void tcp_clearAllChildrenIfServer(TCP* tcp);

gsize tcp_getOutputBufferLength(TCP* tcp);
gsize tcp_getInputBufferLength(TCP* tcp);

void tcp_disableSendBufferAutotuning(TCP* tcp);
void tcp_disableReceiveBufferAutotuning(TCP* tcp);

gboolean tcp_isFamilySupported(TCP* tcp, sa_family_t family);
gboolean tcp_isValidListener(TCP* tcp);
gboolean tcp_isListeningAllowed(TCP* tcp);

gint tcp_shutdown(TCP* tcp, gint how);

void tcp_networkInterfaceIsAboutToSendPacket(TCP* tcp, Packet* packet);

TCPCongestionType tcpCongestion_getType(const gchar* type);

#endif /* SHD_TCP_H_ */
