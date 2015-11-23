/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-action-internal.h"

#include <netinet/in.h>

struct _CreateNodesAction {
    Action super;
    GQuark id;
    GString* requestedIP;
    GString* requestedGeocode;
    GString* requestedType;
    guint64 bandwidthdown;
    guint64 bandwidthup;
    guint64 quantity;
    guint cpuFrequency;
    SimulationTime heartbeatIntervalSeconds;
    GString* heartbeatLogLevelString;
    GString* heartbeatLogInfoString;
    GString* logLevelString;
    GString* logPcapString;
    GString* pcapDirString;
    guint64 socketReceiveBufferSize;
    guint64 socketSendBufferSize;
    guint64 interfaceReceiveBufferLength;

    GList* applications;
    MAGIC_DECLARE;
};

typedef struct _NodeApplication NodeApplication;
struct _NodeApplication {
    GQuark pluginID;
    GString* arguments;
    SimulationTime starttime;
    SimulationTime stoptime;
    MAGIC_DECLARE;
};

RunnableFunctionTable createnodes_functions = {
    (RunnableRunFunc) createnodes_run,
    (RunnableFreeFunc) createnodes_free,
    MAGIC_VALUE
};

CreateNodesAction* createnodes_new(GString* name, GString* ip, GString* geocode, GString* type,
        guint64 bandwidthdown, guint64 bandwidthup, guint64 quantity, guint64 cpuFrequency,
        guint64 heartbeatIntervalSeconds, GString* heartbeatLogLevelString, GString* heartbeatLogInfoString,
        GString* logLevelString, GString* logPcapString, GString* pcapDirString,
        guint64 socketReceiveBufferSize, guint64 socketSendBufferSize, guint64 interfaceReceiveBufferLength)
{
    utility_assert(name);
    CreateNodesAction* action = g_new0(CreateNodesAction, 1);
    MAGIC_INIT(action);

    action_init(&(action->super), &createnodes_functions);

    action->id = g_quark_from_string((const gchar*) name->str);

    action->bandwidthdown = bandwidthdown;
    action->bandwidthup = bandwidthup;
    action->quantity = quantity ? quantity : 1;
    action->cpuFrequency = (guint)cpuFrequency;
    action->heartbeatIntervalSeconds = heartbeatIntervalSeconds;

    /* ignore 127.0.0.1 ip address settings - that is reserved for internal use */
    if(ip && address_stringToIP(ip->str) != address_stringToIP("127.0.0.1")) {
        action->requestedIP = g_string_new(ip->str);
    }
    if(geocode) {
        action->requestedGeocode = g_string_new(geocode->str);
    }
    if(type) {
        action->requestedType = g_string_new(type->str);
    }
    if(heartbeatLogLevelString) {
        action->heartbeatLogLevelString = g_string_new(heartbeatLogLevelString->str);
    }
    if(heartbeatLogInfoString) {
        action->heartbeatLogInfoString = g_string_new(heartbeatLogInfoString->str);
    }
    if(logLevelString) {
        action->logLevelString = g_string_new(logLevelString->str);
    }
    if(logPcapString) {
        action->logPcapString = g_string_new(logPcapString->str);
    }
    if(pcapDirString) {
        action->pcapDirString = g_string_new(pcapDirString->str);
    }
    if(socketReceiveBufferSize) {
        action->socketReceiveBufferSize = socketReceiveBufferSize;
    }
    if(socketSendBufferSize) {
        action->socketSendBufferSize = socketSendBufferSize;
    }
    if(interfaceReceiveBufferLength) {
        action->interfaceReceiveBufferLength = interfaceReceiveBufferLength;
    }

    return action;
}

void createnodes_addApplication(CreateNodesAction* action, GString* pluginName,
        GString* arguments, guint64 starttime, guint64 stoptime)
{
    utility_assert(pluginName && arguments);
    MAGIC_ASSERT(action);

    NodeApplication* nodeApp = g_new0(NodeApplication, 1);

    nodeApp->pluginID = g_quark_from_string((const gchar*) pluginName->str);
    nodeApp->arguments = g_string_new(arguments->str);
    nodeApp->starttime = (SimulationTime) (starttime * SIMTIME_ONE_SECOND);
    nodeApp->stoptime = (SimulationTime) (stoptime * SIMTIME_ONE_SECOND);

    action->applications = g_list_append(action->applications, nodeApp);
}

void createnodes_run(CreateNodesAction* action) {
    MAGIC_ASSERT(action);

    Configuration* config = worker_getConfig();

    const gchar* hostname = g_quark_to_string(action->id);
    guint hostnameCounter = 0;

    if(!hostname) {
        critical("Can not create %"G_GUINT64_FORMAT" Node(s) '%s' with NULL components. Check XML file for errors.",
                action->quantity, g_quark_to_string(action->id));
        return;
    }

    /* if they didnt specify a CPU frequency, use the frequency of the box we are running on */
    guint cpuFrequency = action->cpuFrequency;
    if(!cpuFrequency) {
        cpuFrequency = worker_getRawCPUFrequency();
        if(!cpuFrequency) {
            cpuFrequency = 2500000; /* 2.5 GHz */
            debug("both configured and raw cpu frequencies unavailable, using 2500000 KHz");
        }
    }
    gint cpuThreshold = config->cpuThreshold;
    gint cpuPrecision = config->cpuPrecision;


    /* set node-specific settings if we have them.
     * the node-specific settings should be 0 if its not set so we know to check
     * the global settings later. we avoid using globals here to avoid
     * updating all the nodes if the globals changes during execution.
     */
    SimulationTime heartbeatInterval = 0;
    if(action->heartbeatIntervalSeconds) {
        heartbeatInterval = action->heartbeatIntervalSeconds * SIMTIME_ONE_SECOND;
    }
    GLogLevelFlags heartbeatLogLevel = 0;
    if(action->heartbeatLogLevelString) {
        heartbeatLogLevel = configuration_getLevel(config, action->heartbeatLogLevelString->str);
    }
    gchar* heartbeatLogInfo = NULL;
    if(action->heartbeatLogInfoString) {
        heartbeatLogInfo = g_strdup(action->heartbeatLogInfoString->str);
    }
    GLogLevelFlags logLevel = 0;
    if(action->logLevelString) {
        logLevel = configuration_getLevel(config, action->logLevelString->str);
    }

    gboolean logPcap = FALSE;
    if(action->logPcapString && !g_ascii_strcasecmp(action->logPcapString->str, "true")) {
        logPcap = TRUE;
    }
    
    gchar* pcapDir = NULL;
    if (action->pcapDirString) {
        pcapDir = g_strdup(action->pcapDirString->str);
    }

    gchar* qdisc = configuration_getQueuingDiscipline(config);

    guint64 receiveBufferSize = action->socketReceiveBufferSize; /* bytes */
    gboolean autotuneReceiveBuffer = FALSE;
    if(!receiveBufferSize) {
        receiveBufferSize = worker_getConfig()->initialSocketReceiveBufferSize;
        autotuneReceiveBuffer = worker_getConfig()->autotuneSocketReceiveBuffer;
    }
    guint64 sendBufferSize = action->socketSendBufferSize; /* bytes */
    gboolean autotuneSendBuffer = FALSE;
    if(!sendBufferSize) {
        sendBufferSize = worker_getConfig()->initialSocketSendBufferSize;
        autotuneSendBuffer = worker_getConfig()->autotuneSocketSendBuffer;
    }
    guint64 interfaceReceiveLength = action->interfaceReceiveBufferLength; /* N packets */
    if(!interfaceReceiveLength) {
        interfaceReceiveLength = worker_getConfig()->interfaceBufferSize;
    }

    const gchar* dataDirPath = worker_getHostsRootPath();

    for(gint i = 0; i < action->quantity; i++) {
        /* hostname */
        GString* hostnameBuffer = g_string_new(hostname);
        if(action->quantity > 1) {
            gchar prefix[20];
            g_snprintf(prefix, 20, "%u", ++hostnameCounter);
            hostnameBuffer = g_string_append(hostnameBuffer, (const char*) prefix);
        }
        GQuark id = g_quark_from_string((const gchar*) hostnameBuffer->str);

        /* the node is part of the internet */
        guint nodeSeed = (guint) worker_nextRandomInt();

        Host* host = host_new(id, hostnameBuffer->str,
                action->requestedIP ? action->requestedIP->str : NULL,
                action->requestedGeocode ? action->requestedGeocode->str : NULL,
                action->requestedType ? action->requestedType->str : NULL,
                action->bandwidthdown, action->bandwidthup,
                cpuFrequency, cpuThreshold, cpuPrecision, nodeSeed,
                heartbeatInterval, heartbeatLogLevel, heartbeatLogInfo,
                logLevel, logPcap, pcapDir, qdisc,
                receiveBufferSize, autotuneReceiveBuffer, sendBufferSize, autotuneSendBuffer,
                interfaceReceiveLength, dataDirPath);

        /* save the node somewhere */
        worker_addHost(host, (guint) id);

        g_string_free(hostnameBuffer, TRUE);

        /* loop through and create, add, and boot all applications */
        GList* item = action->applications;
        while (item && item->data) {
            NodeApplication* app = (NodeApplication*) item->data;

            /* make sure our bootstrap events are set properly */
            worker_setCurrentTime(0);
            host_addApplication(host, app->pluginID,
                    app->starttime, app->stoptime, app->arguments->str);
            worker_setCurrentTime(SIMTIME_INVALID);

            item = g_list_next(item);
        }

        /* make sure our bootstrap events are set properly */
        worker_setCurrentTime(0);
        HeartbeatEvent* heartbeat = heartbeat_new(host_getTracker(host));
        worker_scheduleEvent((Event*)heartbeat, heartbeatInterval, id);
        worker_setCurrentTime(SIMTIME_INVALID);
    }
}

void createnodes_free(CreateNodesAction* action) {
    MAGIC_ASSERT(action);

    if(action->requestedIP) {
        g_string_free(action->requestedIP, TRUE);
    }
    if(action->requestedGeocode) {
        g_string_free(action->requestedGeocode, TRUE);
    }
    if(action->requestedType) {
        g_string_free(action->requestedType, TRUE);
    }
    if(action->logLevelString) {
        g_string_free(action->logLevelString, TRUE);
    }
    if(action->heartbeatLogLevelString) {
        g_string_free(action->heartbeatLogLevelString, TRUE);
    }
    if(action->heartbeatLogInfoString) {
        g_string_free(action->heartbeatLogInfoString, TRUE);
    }
    if(action->logPcapString) {
        g_string_free(action->logPcapString, TRUE);
    }
    if(action->pcapDirString) {
        g_string_free(action->pcapDirString, TRUE);
    }

    GList* item = action->applications;
    while (item && item->data) {
        NodeApplication* nodeApp = (NodeApplication*) item->data;
        g_string_free(nodeApp->arguments, TRUE);
        g_free(nodeApp);
        item = g_list_next(item);
    }
    g_list_free(action->applications);

    MAGIC_CLEAR(action);
    g_free(action);
}
