/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHADOW_H_
#define SHADOW_H_

#include <glib.h>
#include <gmodule.h>
#include <igraph/igraph.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <math.h>

#include "shd-config.h"

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
#include "configuration/shd-examples.h"
#include "configuration/shd-configuration.h"
#include "runnable/shd-listener.h"
#include "host/shd-protocol.h"
#include "host/descriptor/shd-descriptor.h"
#include "runnable/shd-runnable.h"
#include "runnable/event/shd-event.h"
#include "runnable/action/shd-action.h"
#include "configuration/shd-parser.h"
#include "host/shd-packet.h"
#include "host/shd-cpu.h"

/* utilities with limited dependencies */
#include "utility/shd-utility.h"
#include "utility/shd-registry.h"
#include "utility/shd-cdf.h"
#include "utility/shd-byte-queue.h"
#include "utility/shd-priority-queue.h"
#include "utility/shd-async-priority-queue.h"
#include "utility/shd-count-down-latch.h"
#include "utility/shd-random.h"

#include "engine/shd-event-queue.h"
#include "plugins/shd-library.h"
#include "engine/shd-plugin.h"

#include "topology/shd-address.h"
#include "topology/shd-poi.h"
#include "topology/shd-pop.h"
#include "topology/shd-path.h"
#include "topology/shd-topology.h"

#include "topology/shd-network.h"
#include "topology/shd-link.h"
#include "topology/shd-internetwork.h"

#include "host/descriptor/shd-epoll.h"
#include "host/descriptor/shd-transport.h"
#include "host/descriptor/shd-channel.h"
#include "host/descriptor/shd-socket.h"
#include "host/descriptor/shd-tcp.h"
#include "host/descriptor/shd-udp.h"
#include "host/shd-application.h"
#include "host/shd-network-interface.h"
#include "host/shd-tracker.h"
#include "engine/shd-system.h"
#include "host/shd-host.h"

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
#include "runnable/action/shd-connect-network.h"
#include "runnable/action/shd-create-network.h"
#include "runnable/action/shd-create-node.h"
#include "runnable/action/shd-generate-cdf.h"
#include "runnable/action/shd-kill-engine.h"
#include "runnable/action/shd-load-cdf.h"
#include "runnable/action/shd-load-plugin.h"
#include "runnable/action/shd-load-topology.h"

#include "engine/shd-logging.h"
#include "engine/shd-engine.h"
#include "engine/shd-worker.h"

extern Engine* shadow_engine;

#endif /* SHADOW_H_ */
