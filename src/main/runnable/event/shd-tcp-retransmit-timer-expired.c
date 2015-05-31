/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _TCPRetransmitTimerExpiredEvent {
    Event super;
    TCP* tcp;
    MAGIC_DECLARE;
};

EventFunctionTable tcpretransmittimerexpired_functions = {
    (EventRunFunc) tcpretransmittimerexpired_run,
    (EventFreeFunc) tcpretransmittimerexpired_free,
    MAGIC_VALUE
};

TCPRetransmitTimerExpiredEvent* tcpretransmittimerexpired_new(TCP* tcp) {
    TCPRetransmitTimerExpiredEvent* event = g_new0(TCPRetransmitTimerExpiredEvent, 1);
    MAGIC_INIT(event);

    shadowevent_init(&(event->super), &tcpretransmittimerexpired_functions);
    descriptor_ref(tcp);
    event->tcp = tcp;

    return event;
}

void tcpretransmittimerexpired_run(TCPRetransmitTimerExpiredEvent* event, Host* node) {
    MAGIC_ASSERT(event);

    debug("event started");

    tcp_retransmitTimerExpired(event->tcp);

    debug("event finished");
}

void tcpretransmittimerexpired_free(TCPRetransmitTimerExpiredEvent* event) {
    MAGIC_ASSERT(event);

    descriptor_unref(event->tcp);

    MAGIC_CLEAR(event);
    g_free(event);
}
