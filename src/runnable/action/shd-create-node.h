/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CREATE_NODES_H_
#define SHD_CREATE_NODES_H_

#include "shadow.h"

typedef struct _CreateNodesAction CreateNodesAction;

CreateNodesAction* createnodes_new(GString* name, GString* ip, GString* geocode, GString* type,
        guint64 bandwidthdown, guint64 bandwidthup, guint64 quantity, guint64 cpuFrequency,
        guint64 heartbeatIntervalSeconds, GString* heartbeatLogLevelString, GString* heartbeatLogInfoString,
        GString* logLevelString, GString* logPcapString, GString* pcapDirString,
        guint64 socketReceiveBufferSize, guint64 socketSendBufferSize, guint64 interfaceReceiveBufferLength);
void createnodes_addApplication(CreateNodesAction* action, GString* pluginName,
        GString* arguments, guint64 starttime, guint64 stoptime);
void createnodes_run(CreateNodesAction* action);
void createnodes_free(CreateNodesAction* action);

#endif /* SHD_CREATE_NODES_H_ */
