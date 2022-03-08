/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_NETWORK_INTERFACE_H_
#define SHD_NETWORK_INTERFACE_H_

#include <glib.h>
#include <netinet/in.h>

#include "main/core/support/definitions.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/socket.h"
#include "main/host/protocol.h"
#include "main/routing/address.h"
#include "main/routing/router.h"

typedef struct _NetworkInterface NetworkInterface;

NetworkInterface* networkinterface_new(Host* host, Address* address, guint64 bwDownKiBps,
                                       guint64 bwUpKiBps, gchar* pcapDir, guint32 pcapCaptureSize,
                                       QDiscMode qdisc, guint64 interfaceReceiveLength);
void networkinterface_free(NetworkInterface* interface);

Address* networkinterface_getAddress(NetworkInterface* interface);
guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface);
guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface);

gboolean networkinterface_isAssociated(NetworkInterface* interface, ProtocolType type,
        in_port_t port, in_addr_t peerAddr, in_port_t peerPort);

void networkinterface_associate(NetworkInterface* interface, const CompatSocket* socket);
void networkinterface_disassociate(NetworkInterface* interface, const CompatSocket* socket);

void networkinterface_wantsSend(NetworkInterface* interface, Host* host,
                                const CompatSocket* socket);
void networkinterface_sent(NetworkInterface* interface);

void networkinterface_startRefillingTokenBuckets(NetworkInterface* interface, Host* host);

void networkinterface_setRouter(NetworkInterface* interface, Router* router);
Router* networkinterface_getRouter(NetworkInterface* interface);

void networkinterface_receivePackets(NetworkInterface* interface, Host* host);

#endif /* SHD_NETWORK_INTERFACE_H_ */
