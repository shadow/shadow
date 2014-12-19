/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_EPOLL_H_
#define SHD_EPOLL_H_

#include "shadow.h"

typedef struct _Epoll Epoll;

/* free this with descriptor_free() */
Epoll* epoll_new(gint handle);

gint epoll_control(Epoll* epoll, gint operation, Descriptor* descriptor,
        struct epoll_event* event);
gint epoll_controlOS(Epoll* epoll, gint operation, gint fileDescriptor,
        struct epoll_event* event);
gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray,
        gint eventArrayLength, gint* nEvents);

void epoll_descriptorStatusChanged(Epoll* epoll, Descriptor* descriptor);
void epoll_tryNotify(Epoll* epoll);

#endif /* SHD_EPOLL_H_ */
