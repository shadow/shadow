/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHADOW_H_
#define SHADOW_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <pthread.h>

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
#include "core/support/shd-definitions.h"
#include "core/shd-main.h"

/* logging */
#include "core/logger/shd-log-level.h"
#include "core/logger/shd-log-record.h"
#include "core/logger/shd-logger-helper.h"
#include "core/logger/shd-logger.h"

/* configuration, base runnables, and input parsing */
#include "core/support/shd-object-counter.h"
#include "core/support/shd-examples.h"
#include "core/support/shd-options.h"
#include "utility/shd-utility.h"
#include "core/work/shd-task.h"
#include "core/work/shd-event.h"
#include "core/work/shd-message.h"
#include "host/shd-protocol.h"
#include "host/descriptor/shd-descriptor.h"
#include "core/support/shd-configuration.h"
#include "routing/shd-payload.h"
#include "routing/shd-packet.h"
#include "host/shd-cpu.h"
#include "utility/shd-pcap-writer.h"

/* utilities with limited dependencies */
#include "utility/shd-byte-queue.h"
#include "utility/shd-priority-queue.h"
#include "utility/shd-async-priority-queue.h"
#include "utility/shd-count-down-latch.h"
#include "utility/shd-random.h"

#include "routing/shd-address.h"
#include "routing/shd-dns.h"
#include "routing/shd-path.h"
#include "routing/shd-topology.h"

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
#include "host/shd-process.h"
#include "host/shd-network-interface.h"
#include "host/shd-tracker.h"
#include "host/shd-host.h"


#include "core/scheduler/shd-scheduler-policy.h"
#include "core/scheduler/shd-scheduler.h"
#include "core/shd-master.h"
#include "core/shd-slave.h"
#include "core/shd-worker.h"

#endif /* SHADOW_H_ */
