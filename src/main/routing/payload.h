/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_ROUTING_SHD_PAYLOAD_H_
#define SRC_MAIN_ROUTING_SHD_PAYLOAD_H_

#include <glib.h>

#include "main/bindings/c/bindings-opaque.h"

typedef struct _Payload Payload;

Payload* payload_new(const Thread* thread, UntypedForeignPtr data, gsize dataLength);
Payload* payload_newWithMemoryManager(UntypedForeignPtr data, gsize dataLength,
                                      const MemoryManager* mem);
Payload* payload_newFromShadow(const void* data, gsize dataLength);

void payload_ref(Payload* payload);
void payload_unref(Payload* payload);

gsize payload_getLength(Payload* payload);
gssize payload_getData(Payload* payload, const Thread* thread, gsize offset,
                       UntypedForeignPtr destBuffer, gsize destBufferLength);
gssize payload_getDataWithMemoryManager(Payload* payload, gsize offset,
                                        UntypedForeignPtr destBuffer, gsize destBufferLength,
                                        MemoryManager* mem);

gsize payload_getDataShadow(Payload* payload, gsize offset, void* destBuffer,
                            gsize destBufferLength);

#endif /* SRC_MAIN_ROUTING_SHD_PAYLOAD_H_ */
