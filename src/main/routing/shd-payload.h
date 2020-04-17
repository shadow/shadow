/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_ROUTING_SHD_PAYLOAD_H_
#define SRC_MAIN_ROUTING_SHD_PAYLOAD_H_

#include <glib.h>

typedef struct _Payload Payload;

Payload* payload_new(gconstpointer data, gsize dataLength);

void payload_ref(Payload* payload);
void payload_unref(Payload* payload);

gsize payload_getLength(Payload* payload);
gsize payload_getData(Payload* payload, gsize offset, gpointer destBuffer, gsize destBufferLength);

#endif /* SRC_MAIN_ROUTING_SHD_PAYLOAD_H_ */
