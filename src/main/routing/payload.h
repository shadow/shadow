/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_ROUTING_SHD_PAYLOAD_H_
#define SRC_MAIN_ROUTING_SHD_PAYLOAD_H_

#include <glib.h>

#include "main/bindings/c/bindings-opaque.h"
#include "main/host/syscall_types.h"

typedef struct _Payload Payload;

Payload* payload_new(const Thread* thread, PluginVirtualPtr data, gsize dataLength);

void payload_ref(Payload* payload);
void payload_unref(Payload* payload);

gsize payload_getLength(Payload* payload);
gssize payload_getData(Payload* payload, const Thread* thread, gsize offset, PluginVirtualPtr destBuffer,
                       gsize destBufferLength);

gsize payload_getDataShadow(Payload* payload, gsize offset, void* destBuffer,
                            gsize destBufferLength);

#endif /* SRC_MAIN_ROUTING_SHD_PAYLOAD_H_ */
