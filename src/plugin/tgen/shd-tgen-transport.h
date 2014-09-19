/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_TRANSPORT_H_
#define SHD_TGEN_TRANSPORT_H_

#include "shd-tgen.h"

typedef enum _TGenTransportProtocol {
    TGEN_PROTOCOL_NONE, TGEN_PROTOCOL_TCP, TGEN_PROTOCOL_UDP,
    TGEN_PROTOCOL_PIPE, TGEN_PROTOCOL_SOCKETPAIR,
} TGenTransportProtocol;

typedef struct _TGenTransport TGenTransport;

TGenTransport* tgentransport_new(gint socketD, const TGenPeer proxy, const TGenPeer peer);
void tgentransport_ref(TGenTransport* transport);
void tgentransport_unref(TGenTransport* transport);

void tgentransport_setCommand(TGenTransport* transport, TGenTransferCommand command,
        GHookFunc onCommandComplete, gpointer hookData);

TGenTransferStatus tgentransport_activate(TGenTransport* transport);
gint tgentransport_getEpollDescriptor(TGenTransport* transport);

#endif /* SHD_TGEN_TRANSPORT_H_ */
