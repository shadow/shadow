/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TRANSPORT_H_
#define SHD_TRANSPORT_H_

#include <glib.h>
#include <netinet/in.h>

#include "core/support/shd-definitions.h"
#include "host/descriptor/shd-descriptor.h"

typedef struct _Transport Transport;
typedef struct _TransportFunctionTable TransportFunctionTable;

typedef gssize (*TransportSendFunc)(Transport* transport, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port);
typedef gssize (*TransportReceiveFunc)(Transport* transport, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port);

struct _TransportFunctionTable {
    DescriptorFunc close;
    DescriptorFunc free;
    TransportSendFunc send;
    TransportReceiveFunc receive;
    MAGIC_DECLARE;
};

struct _Transport {
    Descriptor super;
    TransportFunctionTable* vtable;

    MAGIC_DECLARE;
};

void transport_init(Transport* transport, TransportFunctionTable* vtable, DescriptorType type, gint handle);

gssize transport_sendUserData(Transport* transport, gconstpointer buffer, gsize nBytes,
        in_addr_t ip, in_port_t port);
gssize transport_receiveUserData(Transport* transport, gpointer buffer, gsize nBytes,
        in_addr_t* ip, in_port_t* port);

#endif /* SHD_TRANSPORT_H_ */
