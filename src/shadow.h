/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHADOW_H_
#define SHADOW_H_

#include <glib.h>
#include <gmodule.h>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
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
#include "node/shd-protocol.h"
#include "node/descriptor/shd-descriptor.h"
#include "runnable/shd-runnable.h"
#include "runnable/event/shd-event.h"
#include "runnable/action/shd-action.h"
#include "configuration/shd-parser.h"
#include "node/shd-packet.h"
#include "node/shd-cpu.h"

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
#include "topology/shd-network.h"
#include "topology/shd-link.h"
#include "topology/shd-internetwork.h"

#include "node/descriptor/shd-epoll.h"
#include "node/descriptor/shd-transport.h"
#include "node/descriptor/shd-channel.h"
#include "node/descriptor/shd-socket.h"
#include "node/descriptor/shd-tcp.h"
#include "node/descriptor/shd-udp.h"
#include "node/shd-application.h"
#include "node/shd-network-interface.h"
#include "node/shd-tracker.h"
#include "engine/shd-system.h"
#include "node/shd-node.h"

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

#include "engine/shd-logging.h"
#include "engine/shd-engine.h"
#include "engine/shd-worker.h"

#include "intercept/preload.h"

extern Engine* shadow_engine;

#endif /* SHADOW_H_ */
