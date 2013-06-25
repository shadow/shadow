/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_INTERFACE_SENT_H_
#define SHD_INTERFACE_SENT_H_

#include "shadow.h"

typedef struct _InterfaceSentEvent InterfaceSentEvent;

InterfaceSentEvent* interfacesent_new(NetworkInterface* interface);
void interfacesent_run(InterfaceSentEvent* event, Node* node);
void interfacesent_free(InterfaceSentEvent* event);

#endif /* SHD_INTERFACE_SENT_H_ */
