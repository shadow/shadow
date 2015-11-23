/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHADOW_H_
#define SHADOW_H_

#include <glib.h>
#include <gmodule.h>
#include <igraph.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <poll.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <math.h>

#include "shd-config.h"

// TODO put into a shd-types.h file
typedef struct _Process Process;

/**
 * @mainpage Shadow Documentation
 *
 * @section dep Dependencies
 *
 * We depend on GLib 2.0
 *
 * @section install Installation
 *
 * Once satisfying the dependencies, Shadow can be installed as follows.
 *
 * @section notes Notes
 *
 * Be aware of system limits that could affect your application.
 * @include extra.txt
 */

/*
 * order of includes is very important to prevent circular dependencies.
 * place base classes with few dependencies first.
 */

#include "engine/shd-main.h"

/* configuration, base runnables, and input parsing */
#include "support/shd-examples.h"
#include "support/shd-configuration.h"
#include "utility/shd-utility.h"
#include "runnable/shd-listener.h"
#include "host/shd-protocol.h"
#include "host/descriptor/shd-descriptor.h"
#include "runnable/shd-runnable.h"
#include "runnable/event/shd-event.h"
#include "runnable/action/shd-action.h"
#include "support/shd-parser.h"
#include "host/shd-packet.h"
#include "host/shd-cpu.h"
#include "support/shd-pcap-writer.h"

/* utilities with limited dependencies */
#include "utility/shd-byte-queue.h"
#include "utility/shd-priority-queue.h"
#include "utility/shd-async-priority-queue.h"
#include "utility/shd-count-down-latch.h"
#include "utility/shd-random.h"

#include "support/shd-event-queue.h"

#include "topology/shd-address.h"
#include "topology/shd-dns.h"
#include "topology/shd-path.h"

#include "host/descriptor/shd-epoll.h"
#include "host/descriptor/shd-timer.h"
#include "host/descriptor/shd-transport.h"
#include "host/descriptor/shd-channel.h"
#include "host/descriptor/shd-socket.h"
#include "host/descriptor/shd-tcp.h"
#include "host/descriptor/shd-tcp-congestion.h"
#include "host/descriptor/shd-tcp-aimd.h"
#include "host/descriptor/shd-tcp-reno.h"
#include "host/descriptor/shd-tcp-cubic.h"
#include "host/descriptor/shd-tcp-scoreboard.h"
#include "host/descriptor/shd-udp.h"
#include "host/shd-program.h"
#include "host/shd-process.h"
#include "host/shd-network-interface.h"
#include "host/shd-tracker.h"
#include "host/shd-host.h"

#include "topology/shd-topology.h"

#include "runnable/event/shd-heartbeat.h"
#include "runnable/event/shd-callback.h"
#include "runnable/event/shd-notify-plugin.h"
#include "runnable/event/shd-interface-received.h"
#include "runnable/event/shd-interface-sent.h"
#include "runnable/event/shd-packet-arrived.h"
#include "runnable/event/shd-packet-dropped.h"
#include "runnable/event/shd-start-application.h"
#include "runnable/event/shd-stop-application.h"
#include "runnable/event/shd-tcp-close-timer-expired.h"
#include "runnable/event/shd-tcp-retransmit-timer-expired.h"
#include "runnable/action/shd-create-node.h"
#include "runnable/action/shd-kill-engine.h"
#include "runnable/action/shd-load-plugin.h"
#include "runnable/action/shd-load-topology.h"

#include "support/shd-logging.h"
#include "engine/shd-master.h"
#include "engine/shd-slave.h"
#include "engine/shd-worker.h"

#endif /* SHADOW_H_ */
