/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_HOST_PARAMETERS_H
#define SHD_HOST_PARAMETERS_H

#include <glib.h>

#include "lib/logger/log_level.h"
#include "main/core/support/definitions.h"
#include "main/host/tracker_types.h"

typedef struct _HostParameters HostParameters;
struct _HostParameters {
    GQuark id;
    guint nodeSeed;
    gchar* hostname;
    guint nodeId;
    in_addr_t ipAddr;
    guint64 requestedBwDownBits;
    guint64 requestedBwUpBits;
    guint64 cpuFrequency;
    guint64 cpuThreshold;
    guint64 cpuPrecision;
    SimulationTime heartbeatInterval;
    LogLevel heartbeatLogLevel;
    LogInfoFlags heartbeatLogInfo;
    LogLevel logLevel;
    gchar* pcapDir;
    QDiscMode qdisc;
    guint64 recvBufSize;
    gboolean autotuneRecvBuf;
    guint64 sendBufSize;
    gboolean autotuneSendBuf;
    guint64 interfaceBufSize;
};

#endif
