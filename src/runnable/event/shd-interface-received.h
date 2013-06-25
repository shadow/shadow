/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_INTERFACE_RECEIVED_H_
#define SHD_INTERFACE_RECEIVED_H_

#include "shadow.h"

typedef struct _InterfaceReceivedEvent InterfaceReceivedEvent;

InterfaceReceivedEvent* interfacereceived_new(NetworkInterface* interface);
void interfacereceived_run(InterfaceReceivedEvent* event, Node* node);
void interfacereceived_free(InterfaceReceivedEvent* event);

#endif /* SHD_INTERFACE_RECEIVED_H_ */
