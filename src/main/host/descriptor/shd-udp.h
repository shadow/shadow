/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_UDP_H_
#define SHD_UDP_H_

#include <glib.h>

typedef struct _UDP UDP;

UDP* udp_new(gint handle, guint receiveBufferSize, guint sendBufferSize);

#endif /* SHD_UDP_H_ */
