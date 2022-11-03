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

#include "main/core/support/definitions.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/socket.h"
#include "main/host/protocol.h"
#include "main/routing/address.h"

NetworkInterface* networkinterface_new(Address* address, const gchar* pcapDir,
                                       guint32 pcapCaptureSize, QDiscMode qdisc, bool uses_router);
void networkinterface_free(NetworkInterface* interface);

/* The address and ports must be in network byte order. */
gboolean networkinterface_isAssociated(NetworkInterface* interface, ProtocolType type,
        in_port_t port, in_addr_t peerAddr, in_port_t peerPort);

void networkinterface_associate(NetworkInterface* interface, const CompatSocket* socket);
void networkinterface_disassociate(NetworkInterface* interface, const CompatSocket* socket);

void networkinterface_wantsSend(NetworkInterface* interface, const Host* host,
                                const CompatSocket* socket);

void networkinterface_startRefillingTokenBuckets(NetworkInterface* interface, const Host* host,
                                                 uint64_t bwDownKiBps, uint64_t bwUpKiBps);

void networkinterface_receivePackets(NetworkInterface* interface, const Host* host);

#endif /* SHD_NETWORK_INTERFACE_H_ */
