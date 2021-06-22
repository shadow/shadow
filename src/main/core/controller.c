/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#if !defined __USE_LARGEFILE64
#define __USE_LARGEFILE64
#endif

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stddef.h>
#include <unistd.h>

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "main/core/controller.h"
#include "main/core/manager.h"
#include "main/core/support/definitions.h"
#include "main/host/host.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/topology.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"

struct _Controller {
    /* general options and user configuration for the simulation */
    ConfigOptions* config;

    /* tracks overall wall-clock runtime */
    GTimer* runTimer;

    /* global random source from which all node random sources originate */
    Random* random;

    /* global network connectivity info */
    Topology* topology;
    DNS* dns;

    /* minimum allowed time jump when sending events between nodes */
    SimulationTime minJumpTimeConfig;
    SimulationTime minJumpTime;
    SimulationTime nextMinJumpTime;

    /* start of current window of execution */
    SimulationTime executeWindowStart;
    /* end of current window of execution (start + min_time_jump) */
    SimulationTime executeWindowEnd;
    /* the simulator should attempt to end immediately after this time */
    SimulationTime endTime;

    /* if we run in unlimited bandwidth mode, this is when we go back to bw enforcement */
    SimulationTime bootstrapEndTime;

    Manager* manager;

    MAGIC_DECLARE;
};

// TODO
// static gboolean _controller_handleInterruptSignal(controller* controller) {
//  MAGIC_ASSERT(controller);
//
//  /* handle (SIGHUP, SIGTERM, SIGINT), shutdown cleanly */
//  controller->endTime = 0;
//  controller->killed = TRUE;
//
//  /* dont remove the source */
//  return FALSE;
//}

Controller* controller_new(ConfigOptions* config) {
    utility_assert(config);

    /* Don't do anything in this function that will cause a log message. The
     * global engine is still NULL since we are creating it now, and logging
     * here will cause an assertion error.
     */

    Controller* controller = g_new0(Controller, 1);
    MAGIC_INIT(controller);

    controller->config = config;
    controller->random = random_new(config_getSeed(config));

    controller->minJumpTimeConfig = config_getRunahead(config);

    /* these are only avail in glib >= 2.30
     * setup signal handlers for gracefully handling shutdowns */
    //  TODO
    //  g_unix_signal_add(SIGTERM, (GSourceFunc)_controller_handleInterruptSignal, controller);
    //  g_unix_signal_add(SIGHUP, (GSourceFunc)_controller_handleInterruptSignal, controller);
    //  g_unix_signal_add(SIGINT, (GSourceFunc)_controller_handleInterruptSignal, controller);

    info("simulation controller created");
    return controller;
}

void controller_free(Controller* controller) {
    MAGIC_ASSERT(controller);

    if (controller->topology) {
        topology_free(controller->topology);
    }
    if (controller->dns) {
        dns_free(controller->dns);
    }
    if (controller->random) {
        random_free(controller->random);
    }

    MAGIC_CLEAR(controller);
    g_free(controller);

    info("simulation controller destroyed");
}

static SimulationTime _controller_getMinTimeJump(Controller* controller) {
    MAGIC_ASSERT(controller);

    /* use minimum network latency of our topology
     * if not yet computed, default to 10 milliseconds */
    SimulationTime minJumpTime =
        controller->minJumpTime > 0 ? controller->minJumpTime : 10 * SIMTIME_ONE_MILLISECOND;

    /* if the command line option was given, use that as lower bound */
    if (controller->minJumpTimeConfig > 0 && minJumpTime < controller->minJumpTimeConfig) {
        minJumpTime = controller->minJumpTimeConfig;
    }

    return minJumpTime;
}

void controller_updateMinTimeJump(Controller* controller, gdouble minPathLatency) {
    MAGIC_ASSERT(controller);
    if (controller->nextMinJumpTime == 0 || minPathLatency < controller->nextMinJumpTime) {
        utility_assert(minPathLatency > 0.0f);
        SimulationTime oldJumpMS = controller->nextMinJumpTime;
        controller->nextMinJumpTime = ((SimulationTime)minPathLatency) * SIMTIME_ONE_MILLISECOND;
        debug("updated topology minimum time jump from %" G_GUINT64_FORMAT " to %" G_GUINT64_FORMAT
              " nanoseconds; "
              "the minimum config override is %s (%" G_GUINT64_FORMAT " nanoseconds)",
              oldJumpMS, controller->nextMinJumpTime,
              controller->minJumpTimeConfig > 0 ? "set" : "not set", controller->minJumpTimeConfig);
    }
}

static gboolean _controller_loadTopology(Controller* controller) {
    MAGIC_ASSERT(controller);

    gchar* temporaryFilename =
        utility_getNewTemporaryFilename("shadow-topology-XXXXXX.gml");

    char* topologyString = config_getNetworkGraph(controller->config);

    /* write the topology to a temporary file */
    GError* error = NULL;
    if (!g_file_set_contents(temporaryFilename, topologyString, strlen(topologyString), &error)) {
        utility_panic(
            "unable to write the topology to '%s': %s", temporaryFilename, error->message);
    }

    config_freeString(topologyString);

    /* initialize global routing model */
    controller->topology = topology_new(temporaryFilename, config_getUseShortestPath(controller->config));
    g_unlink(temporaryFilename);

    if (!controller->topology) {
        error("fatal error loading topology at path '%s', check your syntax and try again",
              temporaryFilename);
        g_free(temporaryFilename);
        return FALSE;
    }

    g_free(temporaryFilename);

    /* initialize global DNS addressing */
    controller->dns = dns_new();
    return TRUE;
}

static void _controller_initializeTimeWindows(Controller* controller) {
    MAGIC_ASSERT(controller);

    /* set simulation end time */
    controller->endTime = config_getStopTime(controller->config);

    /* simulation mode depends on configured number of workers */
    guint nWorkers = config_getWorkers(controller->config);
    if (nWorkers > 0) {
        /* multi threaded, manage the other workers */
        controller->executeWindowStart = 0;
        SimulationTime jump = _controller_getMinTimeJump(controller);
        controller->executeWindowEnd = jump;
        controller->nextMinJumpTime = jump;
    } else {
        /* single threaded, we are the only worker */
        controller->executeWindowStart = 0;
        controller->executeWindowEnd = G_MAXUINT64;
    }

    controller->bootstrapEndTime = config_getBootstrapEndTime(controller->config);
}

static void _controller_registerArgCallback(const char* arg, void* _argArray) {
    GPtrArray* argArray = _argArray;

    char* copiedArg = strdup(arg);
    utility_assert(copiedArg != NULL);

    g_ptr_array_add(argArray, copiedArg);
}

typedef struct _ProcessCallbackArgs {
    Controller* controller;
    const char* hostname;
} ProcessCallbackArgs;

static void _controller_registerProcessCallback(const ProcessOptions* proc, void* _callbackArgs) {
    ProcessCallbackArgs* callbackArgs = _callbackArgs;

    char* plugin = processoptions_getPath(proc);
    if (plugin == NULL) {
        utility_panic("The process binary could not be found");
    }

    // build an argv array
    GPtrArray* argArray = g_ptr_array_new();
    g_ptr_array_add(argArray, strdup(plugin));

    // iterate through the arguments and copy them to our array
    processoptions_getArgs(proc, _controller_registerArgCallback, (void*)argArray);

    // the last element of argv must be NULL
    g_ptr_array_add(argArray, NULL);

    // free the GLib array but keep the data
    gchar** argv = (gchar**)g_ptr_array_free(argArray, FALSE);

    guint64 quantity = processoptions_getQuantity(proc);

    char* environment = processoptions_getEnvironment(proc);

    for (guint64 i = 0; i < quantity; i++) {
        manager_addNewVirtualProcess(callbackArgs->controller->manager, callbackArgs->hostname,
                                     plugin, processoptions_getStartTime(proc),
                                     processoptions_getStopTime(proc), argv, environment);
    }

    processoptions_freeString(environment);
    processoptions_freeString(plugin);
    g_strfreev(argv);
}

static void _controller_registerHostCallback(const char* name, const ConfigOptions* config,
                                             const HostOptions* host, void* _controller) {
    Controller* controller = _controller;

    MAGIC_ASSERT(controller);
    utility_assert(host);

    guint managerCpuFreq = manager_getRawCPUFrequency(controller->manager);

    guint64 quantity = hostoptions_getQuantity(host);

    for (guint64 i = 0; i < quantity; i++) {
        HostParameters* params = g_new0(HostParameters, 1);

        GString* hostnameBuffer = g_string_new(name);
        if (quantity > 1) {
            g_string_append_printf(hostnameBuffer, "%" G_GUINT64_FORMAT, i + 1);
        }
        params->hostname = hostnameBuffer->str;

        params->cpuFrequency = MAX(0, managerCpuFreq);
        params->cpuThreshold = 0;
        params->cpuPrecision = 200;

        params->logLevel = hostoptions_getLogLevel(host);
        params->heartbeatLogLevel = hostoptions_getHeartbeatLogLevel(host);
        params->heartbeatLogInfo = hostoptions_getHeartbeatLogInfo(host);
        params->heartbeatInterval = hostoptions_getHeartbeatInterval(host);

        params->pcapDir = hostoptions_getPcapDirectory(host);

        params->ipHint = hostoptions_getIpAddressHint(host);
        params->countrycodeHint = hostoptions_getCountryCodeHint(host);
        params->citycodeHint = hostoptions_getCityCodeHint(host);

        /* shadow uses values in KiB/s, but the config uses b/s */
        /* TODO: use bits or bytes everywhere within Shadow (see also:
         * _topology_findVertexAttributeStringBandwidth()) */
        params->requestedBWDownKiBps = hostoptions_getBandwidthDown(host) / (8 * 1024);
        params->requestedBWUpKiBps = hostoptions_getBandwidthUp(host) / (8 * 1024);

        /* some options come from the config options and not the host options */
        params->sendBufSize = config_getSocketSendBuffer(config);
        params->recvBufSize = config_getSocketRecvBuffer(config);
        params->autotuneSendBuf = config_getSocketSendAutotune(config);
        params->autotuneRecvBuf = config_getSocketRecvAutotune(config);
        params->interfaceBufSize = config_getInterfaceBuffer(config);
        params->qdisc = config_getInterfaceQdisc(config);

        manager_addNewVirtualHost(controller->manager, params);

        ProcessCallbackArgs processArgs;
        processArgs.controller = controller;
        processArgs.hostname = hostnameBuffer->str;

        /* now handle each virtual process the host will run */
        hostoptions_iterProcesses(host, _controller_registerProcessCallback, (void*)&processArgs);

        /* cleanup for next pass through the loop */
        g_string_free(hostnameBuffer, TRUE);

        hostoptions_freeString(params->pcapDir);
        hostoptions_freeString(params->ipHint);
        hostoptions_freeString(params->countrycodeHint);
        hostoptions_freeString(params->citycodeHint);

        g_free(params);
    }
}

static void _controller_registerHosts(Controller* controller) {
    MAGIC_ASSERT(controller);
    config_iterHosts(controller->config, _controller_registerHostCallback, (void*)controller);
}

gint controller_run(Controller* controller) {
    MAGIC_ASSERT(controller);

    info("loading and initializing simulation data");

    gboolean isSuccess = _controller_loadTopology(controller);
    if (!isSuccess) {
        return 1;
    }

    _controller_initializeTimeWindows(controller);

    /* the controller will be responsible for distributing the actions to the managers so that
     * they all have a consistent view of the simulation, topology, etc.
     * For now we only have one manager so send it everything. */
    guint managerSeed = random_nextUInt(controller->random);
    controller->manager = manager_new(controller, controller->config, controller->endTime,
                                      controller->bootstrapEndTime, managerSeed);

    if (controller->manager == NULL) {
        utility_panic("unable to create manager");
    }

    info("registering plugins and hosts");

    /* register the components needed by each manager.
     * this must be done after managers are available so we can send them messages */
    _controller_registerHosts(controller);

    info("running simulation");

    /* dont buffer log messages in trace mode */
    if (config_getLogLevel(controller->config) != LOGLEVEL_TRACE) {
        info("log message buffering is enabled for efficiency");
        shadow_logger_setEnableBuffering(TRUE);
    }

    /* start running each manager */
    manager_run(controller->manager);

    /* only need to disable buffering if it was enabled, otherwise
     * don't log the message as it may confuse the user. */
    if (config_getLogLevel(controller->config) != LOGLEVEL_TRACE) {
        info("log message buffering is disabled during cleanup");
        shadow_logger_setEnableBuffering(FALSE);
    }

    info("simulation finished, cleaning up now");

    return manager_free(controller->manager);
}

gboolean controller_managerFinishedCurrentRound(Controller* controller,
                                                SimulationTime minNextEventTime,
                                                SimulationTime* executeWindowStart,
                                                SimulationTime* executeWindowEnd) {
    MAGIC_ASSERT(controller);
    utility_assert(executeWindowStart && executeWindowEnd);

    /* TODO: once we get multiple managers, we have to block them here
     * until they have all notified us that they are finished */

    /* update our detected min jump time */
    controller->minJumpTime = controller->nextMinJumpTime;

    /* update the next interval window based on next event times */
    SimulationTime newStart = minNextEventTime;
    SimulationTime newEnd = minNextEventTime + _controller_getMinTimeJump(controller);

    /* update the new window end as one interval past the new window start,
     * making sure we dont run over the experiment end time */
    if (newEnd > controller->endTime) {
        newEnd = controller->endTime;
    }

    /* finally, set the new values */
    controller->executeWindowStart = newStart;
    controller->executeWindowEnd = newEnd;

    *executeWindowStart = controller->executeWindowStart;
    *executeWindowEnd = controller->executeWindowEnd;

    /* return TRUE if we should keep running */
    return newStart < newEnd ? TRUE : FALSE;
}

gdouble controller_getLatency(Controller* controller, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(controller);
    return topology_getLatency(controller->topology, srcAddress, dstAddress);
}

DNS* controller_getDNS(Controller* controller) {
    MAGIC_ASSERT(controller);
    return controller->dns;
}

Topology* controller_getTopology(Controller* controller) {
    MAGIC_ASSERT(controller);
    return controller->topology;
}
