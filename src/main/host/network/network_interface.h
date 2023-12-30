/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_NETWORK_INTERFACE_H_
#define SHD_NETWORK_INTERFACE_H_

#include <glib.h>
#include <netinet/in.h>

typedef struct _NetworkInterface NetworkInterface;

#include "main/bindings/c/bindings.h"
#include "main/core/definitions.h"
#include "main/host/descriptor/socket.h"
#include "main/host/protocol.h"
#include "main/routing/address.h"
#include "main/routing/packet.minimal.h"

NetworkInterface* networkinterface_new(Address* address, const char* name, const gchar* pcapDir,
                                       guint32 pcapCaptureSize, QDiscMode qdisc);
void networkinterface_free(NetworkInterface* interface);

/* The address and ports must be in network byte order. */
gboolean networkinterface_isAssociated(NetworkInterface* interface, ProtocolType type,
                                       in_port_t port, in_addr_t peerAddr, in_port_t peerPort);

void networkinterface_associate(NetworkInterface* interface, const InetSocket* socket,
                                ProtocolType type, in_port_t port, in_addr_t peerIP,
                                in_port_t peerPort);
void networkinterface_disassociate(NetworkInterface* interface, ProtocolType type, in_port_t port,
                                   in_addr_t peerIP, in_port_t peerPort);

void networkinterface_wantsSend(NetworkInterface* interface, const InetSocket* socket);

Packet* networkinterface_pop(NetworkInterface* interface);
void networkinterface_push(NetworkInterface* interface, Packet* packet, CEmulatedTime recvTime);

/* Disassociate all bound sockets and remove sockets from the sending queue. */
void networkinterface_removeAllSockets(NetworkInterface* interface);

#endif /* SHD_NETWORK_INTERFACE_H_ */
