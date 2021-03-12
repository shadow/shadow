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

#include "main/core/controller.h"
#include "main/core/logger/shadow_logger.h"
#include "main/core/manager.h"
#include "main/core/support/configuration.h"
#include "main/core/support/definitions.h"
#include "main/core/support/examples.h"
#include "main/core/support/options.h"
#include "main/host/host.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/topology.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/log_level.h"
#include "support/logger/logger.h"

struct _Controller {
    /* general options and user configuration for the simulation */
    Options* options;
    Configuration* config;

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

Controller* controller_new(Options* options) {
    debug_assert(options);

    /* Don't do anything in this function that will cause a log message. The
     * global engine is still NULL since we are creating it now, and logging
     * here will cause an assertion error.
     */

    Controller* controller = g_new0(Controller, 1);
    MAGIC_INIT(controller);

    controller->options = options;
    controller->random = random_new(options_getRandomSeed(options));

    gint minRunAhead = (SimulationTime)options_getMinRunAhead(options);
    controller->minJumpTimeConfig = ((SimulationTime)minRunAhead) * SIMTIME_ONE_MILLISECOND;

    /* these are only avail in glib >= 2.30
     * setup signal handlers for gracefully handling shutdowns */
    //  TODO
    //  g_unix_signal_add(SIGTERM, (GSourceFunc)_controller_handleInterruptSignal, controller);
    //  g_unix_signal_add(SIGHUP, (GSourceFunc)_controller_handleInterruptSignal, controller);
    //  g_unix_signal_add(SIGINT, (GSourceFunc)_controller_handleInterruptSignal, controller);

    message("simulation controller created");
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
    if (controller->config) {
        configuration_free(controller->config);
    }
    if (controller->random) {
        random_free(controller->random);
    }

    MAGIC_CLEAR(controller);
    g_free(controller);

    message("simulation controller destroyed");
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
        debug_assert(minPathLatency > 0.0f);
        SimulationTime oldJumpMS = controller->nextMinJumpTime;
        controller->nextMinJumpTime = ((SimulationTime)minPathLatency) * SIMTIME_ONE_MILLISECOND;
        info("updated topology minimum time jump from %" G_GUINT64_FORMAT " to %" G_GUINT64_FORMAT
             " nanoseconds; "
             "the minimum config override is %s (%" G_GUINT64_FORMAT " nanoseconds)",
             oldJumpMS, controller->nextMinJumpTime,
             controller->minJumpTimeConfig > 0 ? "set" : "not set", controller->minJumpTimeConfig);
    }
}

static gboolean _controller_loadConfiguration(Controller* controller) {
    MAGIC_ASSERT(controller);

    /* parse built-in examples, or input files */
    GString* file = NULL;
    // add options_doRunTGenExample() option
    if (options_doRunTestExample(controller->options)) {
        /* parse a built-in example */
        file = example_getTestContents();
    } else {
        /* parse Shadow XML config file */
        const GString* fileName = options_getInputXMLFilename(controller->options);
        if (!fileName) {
            critical("unable to obtain the name for the configuration file");
            return FALSE;
        }

        // Read config from file or stdin
        if (0 == g_strcmp0("-", fileName->str)) {
            file = utility_getFileContents("/dev/stdin");
        } else {
            file = utility_getFileContents(fileName->str);
        }

        if (!file) {
            critical("unable to read configuration file contents");
            return FALSE;
        }
    }

    if (file) {
        controller->config = configuration_new(controller->options, file);
        g_string_free(file, TRUE);
        file = NULL;
    }

    /* if there was an error parsing, bounce out */
    if (controller->config) {
        message("successfully parsed Shadow XML input!");
        message("shadow configuration file loaded, parsed, and passed validation");
        return TRUE;
    } else {
        critical("error parsing Shadow XML input!");
        critical("there was a problem parsing the Shadow config file, and we can't run without it");
        return FALSE;
    }
}

static gboolean _controller_loadTopology(Controller* controller) {
    MAGIC_ASSERT(controller);

    ConfigurationTopologyElement* e = configuration_getTopologyElement(controller->config);
    gchar* temporaryFilename =
        utility_getNewTemporaryFilename("shadow-topology-XXXXXX.graphml.xml");

    /* igraph wants a path to a graphml file, prefer a path over cdata */
    if (e->path.isSet) {
        /* now make the configured path exist, pointing to the new file */
        gint result = symlink(e->path.string->str, temporaryFilename);
        if (result < 0) {
            warning(
                "Unable to create symlink at %s pointing to %s; symlink() returned %i error %i: %s",
                temporaryFilename, e->path.string->str, result, errno, g_strerror(errno));
        } else {
            /* that better not be a dangling link */
            g_assert(g_file_test(temporaryFilename, G_FILE_TEST_IS_SYMLINK) &&
                     g_file_test(temporaryFilename, G_FILE_TEST_IS_REGULAR));

            message("new topology file '%s' now linked at '%s'", e->path.string->str,
                    temporaryFilename);
        }
    } else {
        debug_assert(e->cdata.isSet);
        GError* error = NULL;

        /* copy the cdata to the new temporary filename */
        if (!g_file_set_contents(
                temporaryFilename, e->cdata.string->str, (gssize)e->cdata.string->len, &error)) {
            error("unable to write cdata topology to '%s': %s", temporaryFilename, error->message);
            return FALSE;
        }
    }

    /* initialize global routing model */
    controller->topology = topology_new(temporaryFilename);
    g_unlink(temporaryFilename);

    if (!controller->topology) {
        critical("fatal error loading topology at path '%s', check your syntax and try again",
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
    ConfigurationShadowElement* e = configuration_getShadowElement(controller->config);
    controller->endTime = (SimulationTime)(SIMTIME_ONE_SECOND * e->stoptime.integer);

    /* simulation mode depends on configured number of workers */
    guint nWorkers = options_getNWorkerThreads(controller->options);
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

    /* check if we run in unlimited bandwidth mode */
    ConfigurationShadowElement* shadowElm = configuration_getShadowElement(controller->config);

    if (shadowElm && shadowElm->bootstrapEndTime.isSet) {
        controller->bootstrapEndTime =
            (SimulationTime)(SIMTIME_ONE_SECOND * shadowElm->bootstrapEndTime.integer);
    } else {
        controller->bootstrapEndTime = (SimulationTime)0;
    }
}

static void _controller_registerPluginCallback(ConfigurationPluginElement* pe,
                                               Controller* controller) {
    debug_assert(pe);
    MAGIC_ASSERT(controller);
    debug_assert(pe->id.isSet && pe->id.string);
    manager_addNewProgram(controller->manager, pe->id.string->str, pe->path.string->str,
                          pe->startsymbol.isSet ? pe->startsymbol.string->str : NULL);
}

static void _controller_registerPlugins(Controller* controller) {
    MAGIC_ASSERT(controller);

    GQueue* plugins = configuration_getPluginElements(controller->config);
    g_queue_foreach(plugins, (GFunc)_controller_registerPluginCallback, controller);
}

typedef struct _ProcessCallbackArgs {
    Controller* controller;
    HostParameters* hostParams;
} ProcessCallbackArgs;

static void _controller_registerProcessCallback(ConfigurationProcessElement* pe,
                                                ProcessCallbackArgs* args) {
    debug_assert(pe && args);
    MAGIC_ASSERT(args->controller);
    debug_assert(pe->plugin.isSet && pe->plugin.string);
    debug_assert(pe->arguments.isSet && pe->arguments.string);

    manager_addNewVirtualProcess(args->controller->manager, args->hostParams->hostname,
                                 pe->plugin.string->str,
                                 pe->preload.isSet ? pe->preload.string->str : NULL,
                                 SIMTIME_ONE_SECOND * pe->starttime.integer,
                                 pe->stoptime.isSet ? SIMTIME_ONE_SECOND * pe->stoptime.integer : 0,
                                 pe->arguments.string->str);
}

static void _controller_registerHostCallback(ConfigurationHostElement* he, Controller* controller) {
    MAGIC_ASSERT(controller);
    debug_assert(he);
    debug_assert(he->id.isSet && he->id.string);

    guint64 quantity = he->quantity.isSet ? he->quantity.integer : 1;

    for (guint64 i = 0; i < quantity; i++) {
        HostParameters* params = g_new0(HostParameters, 1);

        /* hostname and id params */
        const gchar* hostNameBase = he->id.string->str;

        GString* hostnameBuffer = g_string_new(hostNameBase);
        if (quantity > 1) {
            g_string_append_printf(hostnameBuffer, "%" G_GUINT64_FORMAT, i + 1);
        }
        params->hostname = hostnameBuffer->str;

        /* cpu params - if they didnt specify a CPU frequency, use the manager machine frequency */
        gint managerCPUFreq = manager_getRawCPUFrequency(controller->manager);
        params->cpuFrequency = he->cpufrequency.isSet
                                   ? he->cpufrequency.integer
                                   : (managerCPUFreq > 0) ? (guint64)managerCPUFreq : 0;
        if (params->cpuFrequency == 0) {
            params->cpuFrequency = 2500000; // 2.5 GHz
            debug("both configured and raw manager cpu frequencies unavailable, using 2500000 KHz");
        }

        gint defaultCPUThreshold = options_getCPUThreshold(controller->options);
        params->cpuThreshold = defaultCPUThreshold > 0 ? defaultCPUThreshold : 0;
        gint defaultCPUPrecision = options_getCPUPrecision(controller->options);
        params->cpuPrecision = defaultCPUPrecision > 0 ? defaultCPUPrecision : 0;

        params->logLevel = he->loglevel.isSet ? loglevel_fromStr(he->loglevel.string->str)
                                              : options_getLogLevel(controller->options);

        params->heartbeatLogLevel = he->heartbeatloglevel.isSet
                                        ? loglevel_fromStr(he->heartbeatloglevel.string->str)
                                        : options_getHeartbeatLogLevel(controller->options);

        params->heartbeatInterval =
            he->heartbeatfrequency.isSet
                ? (SimulationTime)(he->heartbeatfrequency.integer * SIMTIME_ONE_SECOND)
                : options_getHeartbeatInterval(controller->options);

        params->heartbeatLogInfo =
            he->heartbeatloginfo.isSet
                ? options_toHeartbeatLogInfo(controller->options, he->heartbeatloginfo.string->str)
                : options_getHeartbeatLogInfo(controller->options);

        params->logPcap =
            (he->logpcap.isSet && !g_ascii_strcasecmp(he->logpcap.string->str, "true")) ? TRUE
                                                                                        : FALSE;
        params->pcapDir = he->pcapdir.isSet ? he->pcapdir.string->str : NULL;

        /* socket buffer settings - if size is set manually, turn off autotuning */
        params->recvBufSize = he->socketrecvbuffer.isSet
                                  ? he->socketrecvbuffer.integer
                                  : options_getSocketReceiveBufferSize(controller->options);
        params->autotuneRecvBuf = he->socketrecvbuffer.isSet
                                      ? FALSE
                                      : options_doAutotuneReceiveBuffer(controller->options);

        params->sendBufSize = he->socketsendbuffer.isSet
                                  ? he->socketsendbuffer.integer
                                  : options_getSocketSendBufferSize(controller->options);
        params->autotuneSendBuf =
            he->socketsendbuffer.isSet ? FALSE : options_doAutotuneSendBuffer(controller->options);

        params->interfaceBufSize = he->interfacebuffer.isSet
                                       ? he->interfacebuffer.integer
                                       : options_getInterfaceBufferSize(controller->options);
        params->qdisc = options_getQueuingDiscipline(controller->options);

        /* requested attributes from shadow config */
        params->ipHint = he->ipHint.isSet ? he->ipHint.string->str : NULL;
        params->countrycodeHint =
            he->countrycodeHint.isSet ? he->countrycodeHint.string->str : NULL;
        params->citycodeHint = he->citycodeHint.isSet ? he->citycodeHint.string->str : NULL;
        params->geocodeHint = he->geocodeHint.isSet ? he->geocodeHint.string->str : NULL;
        params->typeHint = he->typeHint.isSet ? he->typeHint.string->str : NULL;
        params->requestedBWDownKiBps = he->bandwidthdown.isSet ? he->bandwidthdown.integer : 0;
        params->requestedBWUpKiBps = he->bandwidthup.isSet ? he->bandwidthup.integer : 0;

        manager_addNewVirtualHost(controller->manager, params);

        ProcessCallbackArgs processArgs;
        processArgs.controller = controller;
        processArgs.hostParams = params;

        /* now handle each virtual process the host will run */
        g_queue_foreach(he->processes, (GFunc)_controller_registerProcessCallback, &processArgs);

        /* cleanup for next pass through the loop */
        g_string_free(hostnameBuffer, TRUE);
        g_free(params);
    }
}

static void _controller_registerHosts(Controller* controller) {
    MAGIC_ASSERT(controller);
    GQueue* hosts = configuration_getHostElements(controller->config);
    g_queue_foreach(hosts, (GFunc)_controller_registerHostCallback, controller);
}

gint controller_run(Controller* controller) {
    MAGIC_ASSERT(controller);

    message("loading and initializing simulation data");

    /* start loading and initializing simulation data */
    gboolean isSuccess = _controller_loadConfiguration(controller);
    if (!isSuccess) {
        return 1;
    }

    isSuccess = _controller_loadTopology(controller);
    if (!isSuccess) {
        return 1;
    }

    _controller_initializeTimeWindows(controller);

    ConfigurationShadowElement* element = configuration_getShadowElement(controller->config);
    g_assert(element && element->preloadPath.isSet);

    /* the controller will be responsible for distributing the actions to the managers so that
     * they all have a consistent view of the simulation, topology, etc.
     * For now we only have one manager so send it everything. */
    guint managerSeed = random_nextUInt(controller->random);
    controller->manager =
        manager_new(controller, controller->options, controller->endTime,
                    controller->bootstrapEndTime, managerSeed, element->preloadPath.string->str,
                    element->environment.isSet ? element->environment.string->str : NULL);

    message("registering plugins and hosts");

    /* register the components needed by each manager.
     * this must be done after managers are available so we can send them messages */
    _controller_registerPlugins(controller);
    _controller_registerHosts(controller);

    message("running simulation");

    /* dont buffer log messages in debug mode */
    if (options_getLogLevel(controller->options) != LOGLEVEL_DEBUG) {
        message("log message buffering is enabled for efficiency");
        shadow_logger_setEnableBuffering(shadow_logger_getDefault(), TRUE);
    }

    /* start running each manager */
    manager_run(controller->manager);

    /* only need to disable buffering if it was enabled, otherwise
     * don't log the message as it may confuse the user. */
    if (options_getLogLevel(controller->options) != LOGLEVEL_DEBUG) {
        message("log message buffering is disabled during cleanup");
        shadow_logger_setEnableBuffering(shadow_logger_getDefault(), FALSE);
    }

    message("simulation finished, cleaning up now");

    return manager_free(controller->manager);
}

gboolean controller_managerFinishedCurrentRound(Controller* controller,
                                                SimulationTime minNextEventTime,
                                                SimulationTime* executeWindowStart,
                                                SimulationTime* executeWindowEnd) {
    MAGIC_ASSERT(controller);
    debug_assert(executeWindowStart && executeWindowEnd);

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
