/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _TCPCloseTimerExpiredEvent {
    Event super;
    TCP* tcp;
    MAGIC_DECLARE;
};

EventFunctionTable tcpclosetimerexpired_functions = {
    (EventRunFunc) tcpclosetimerexpired_run,
    (EventFreeFunc) tcpclosetimerexpired_free,
    MAGIC_VALUE
};

TCPCloseTimerExpiredEvent* tcpclosetimerexpired_new(TCP* tcp) {
    TCPCloseTimerExpiredEvent* event = g_new0(TCPCloseTimerExpiredEvent, 1);
    MAGIC_INIT(event);

    shadowevent_init(&(event->super), &tcpclosetimerexpired_functions);
    event->tcp = tcp;
    descriptor_ref(tcp);

    return event;
}

void tcpclosetimerexpired_run(TCPCloseTimerExpiredEvent* event, Host* node) {
    MAGIC_ASSERT(event);

    debug("event started");

    tcp_closeTimerExpired(event->tcp);

    debug("event finished");
}

void tcpclosetimerexpired_free(TCPCloseTimerExpiredEvent* event) {
    MAGIC_ASSERT(event);

    descriptor_unref(event->tcp);

    MAGIC_CLEAR(event);
    g_free(event);
}
