/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_EPOLL_H_
#define SHD_EPOLL_H_

#include <glib.h>
#include <sys/epoll.h>

#include "main/host/descriptor/descriptor.h"

typedef struct _Epoll Epoll;

/* free this with descriptor_free() */
Epoll* epoll_new();

gint epoll_control(Epoll* epoll, gint operation, LegacyDescriptor* descriptor,
                   const struct epoll_event* event);
gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray,
        gint eventArrayLength, gint* nEvents);

void epoll_clearWatchListeners(Epoll* epoll);
guint epoll_getNumReadyEvents(Epoll* epoll);

#endif /* SHD_EPOLL_H_ */
