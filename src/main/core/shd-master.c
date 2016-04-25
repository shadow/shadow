/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/* these are only avail in glib >= 2.30, needed for signals */
#include <glib-unix.h>
#include <signal.h>

#include <glib/gstdio.h>

#include "shadow.h"

struct _Master {
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

    Slave* slave;

    MAGIC_DECLARE;
};

// TODO
//static gboolean _master_handleInterruptSignal(Master* master) {
//  MAGIC_ASSERT(master);
//
//  /* handle (SIGHUP, SIGTERM, SIGINT), shutdown cleanly */
//  master->endTime = 0;
//  master->killed = TRUE;
//
//  /* dont remove the source */
//  return FALSE;
//}

Master* master_new(Options* options) {
    utility_assert(options);

    /* Don't do anything in this function that will cause a log message. The
     * global engine is still NULL since we are creating it now, and logging
     * here will cause an assertion error.
     */

    Master* master = g_new0(Master, 1);
    MAGIC_INIT(master);

    master->options = options;
    master->random = random_new(options_getRandomSeed(options));

    gint minRunAhead = (SimulationTime)options_getMinRunAhead(options);
    master->minJumpTimeConfig = ((SimulationTime)minRunAhead) * SIMTIME_ONE_MILLISECOND;

    /* these are only avail in glib >= 2.30
     * setup signal handlers for gracefully handling shutdowns */
//  TODO
//  g_unix_signal_add(SIGTERM, (GSourceFunc)_master_handleInterruptSignal, master);
//  g_unix_signal_add(SIGHUP, (GSourceFunc)_master_handleInterruptSignal, master);
//  g_unix_signal_add(SIGINT, (GSourceFunc)_master_handleInterruptSignal, master);

    message("simulation master created");
    return master;
}

void master_free(Master* master) {
    MAGIC_ASSERT(master);

    if(master->topology) {
        topology_free(master->topology);
    }
    if(master->dns) {
        dns_free(master->dns);
    }
    if(master->random) {
        random_free(master->random);
    }

    MAGIC_CLEAR(master);
    g_free(master);

    message("simulation master destroyed");
}

static SimulationTime _master_getMinTimeJump(Master* master) {
    MAGIC_ASSERT(master);

    /* use minimum network latency of our topology
     * if not yet computed, default to 10 milliseconds */
    SimulationTime minJumpTime = master->minJumpTime > 0 ? master->minJumpTime : 10 * SIMTIME_ONE_MILLISECOND;

    /* if the command line option was given, use that as lower bound */
    if(master->minJumpTimeConfig > 0 && minJumpTime < master->minJumpTimeConfig) {
        minJumpTime = master->minJumpTimeConfig;
    }

    return minJumpTime;
}

void master_updateMinTimeJump(Master* master, gdouble minPathLatency) {
    MAGIC_ASSERT(master);
    if(master->nextMinJumpTime == 0 || minPathLatency < master->nextMinJumpTime) {
        utility_assert(minPathLatency > 0.0f);
        SimulationTime oldJumpMS = master->nextMinJumpTime;
        master->nextMinJumpTime = ((SimulationTime)minPathLatency) * SIMTIME_ONE_MILLISECOND;
        info("updated topology minimum time jump from %"G_GUINT64_FORMAT" to %"G_GUINT64_FORMAT" nanoseconds; "
             "the minimum config override is %s (%"G_GUINT64_FORMAT" nanoseconds)",
                oldJumpMS, master->nextMinJumpTime, master->minJumpTimeConfig > 0 ? "set" : "not set",
                master->minJumpTimeConfig);
    }
}

static void _master_loadConfiguration(Master* master) {
    MAGIC_ASSERT(master);

    /* parse built-in examples, or input files */
    GString* file = NULL;
    // add options_doRunTGenExample() option
    if(options_doRunTestExample(master->options)) {
        /* parse a built-in example */
        file = example_getTestContents();
    } else {
        /* parse Shadow XML config file */
        const GString* fileName = options_getInputXMLFilename(master->options);
        file = utility_getFileContents(fileName->str);
    }

    if(file) {
        master->config = configuration_new(master->options, file);
        g_string_free(file, TRUE);
    }

    /* if there was an error parsing, bounce out */
    if(master->config) {
        message("successfully parsed Shadow XML input!");
    } else {
        error("error parsing Shadow XML input!");
    }
}

static void _master_loadTopology(Master* master) {
    MAGIC_ASSERT(master);

    ConfigurationTopologyElement* e = configuration_getTopologyElement(master->config);
    gchar* temporaryFilename = utility_getNewTemporaryFilename("shadow-topology-XXXXXX.graphml.xml");

    /* igraph wants a path to a graphml file, prefer a path over cdata */
    if(e->path.isSet) {
        /* now make the configured path exist, pointing to the new file */
        gint result = symlink(e->path.string->str, temporaryFilename);
        if(result < 0) {
            warning("Unable to create symlink at %s pointing to %s; symlink() returned %i error %i: %s",
                    temporaryFilename, e->path.string->str,
                    result, errno, g_strerror(errno));
        } else {
            /* that better not be a dangling link */
            g_assert(g_file_test(temporaryFilename, G_FILE_TEST_IS_SYMLINK) &&
                    g_file_test(temporaryFilename, G_FILE_TEST_IS_REGULAR));

            message("new topology file '%s' now linked at '%s'",
                    e->path.string->str, temporaryFilename);
        }
    } else {
        utility_assert(e->cdata.isSet);
        GError* error = NULL;

        /* copy the cdata to the new temporary filename */
        if(!g_file_set_contents(temporaryFilename, e->cdata.string->str,
                (gssize)e->cdata.string->len, &error)) {
            error("unable to write cdata topology to '%s': %s", temporaryFilename, error->message);
            return;
        }
    }

    /* initialize global routing model */
    master->topology = topology_new(temporaryFilename);
    g_unlink(temporaryFilename);
    g_free(temporaryFilename);

    if(!master->topology) {
        error("error loading topology path '%s'", temporaryFilename);
        return;
    }

    /* initialize global DNS addressing */
    master->dns = dns_new();
}

static void _master_initializeTimeWindows(Master* master) {
    MAGIC_ASSERT(master);

    /* set simulation end time */
    ConfigurationKillElement* e = configuration_getKillElement(master->config);
    master->endTime = (SimulationTime) (SIMTIME_ONE_SECOND * e->time.integer);

    /* simulation mode depends on configured number of workers */
    guint nWorkers = options_getNWorkerThreads(master->options);
    if(nWorkers > 0) {
        /* multi threaded, manage the other workers */
        master->executeWindowStart = 0;
        SimulationTime jump = _master_getMinTimeJump(master);
        master->executeWindowEnd = jump;
        master->nextMinJumpTime = jump;
    } else {
        /* single threaded, we are the only worker */
        master->executeWindowStart = 0;
        master->executeWindowEnd = G_MAXUINT64;
    }
}

static void _master_registerPlugins(Master* master) {
    MAGIC_ASSERT(master);

    GList* plugins = configuration_getPluginElements(master->config);
    GList* pluginItem = plugins;
    while(pluginItem != NULL) {
        ConfigurationPluginElement* e = pluginItem->data;
        slave_addNewProgram(master->slave, e->id.string->str, e->path.string->str);
        pluginItem = g_list_next(pluginItem);
    }
}

static void _master_registerHosts(Master* master) {
    MAGIC_ASSERT(master);

    GList* nodes = configuration_getNodeElements(master->config);
    GList* nodeItem = nodes;
    while(nodeItem != NULL) {
        ConfigurationNodeElement* ne = nodeItem->data;
        utility_assert(ne->id.isSet && ne->id.string);

        guint64 quantity = ne->quantity.isSet ? ne->quantity.integer : 1;

        for(guint64 i = 0; i < quantity; i++) {
            HostParameters* params = g_new0(HostParameters, 1);

            /* hostname and id params */
            const gchar* hostNameBase = ne->id.string->str;

            GString* hostnameBuffer = g_string_new(hostNameBase);
            if(quantity > 1) {
                g_string_append_printf(hostnameBuffer, "%"G_GUINT64_FORMAT, i+1);
            }
            params->hostname = hostnameBuffer->str;

            /* cpu params - if they didnt specify a CPU frequency, use the slave machine frequency */
            gint slaveCPUFreq = slave_getRawCPUFrequency(master->slave);
            params->cpuFrequency = ne->cpufrequency.isSet ? ne->cpufrequency.integer : (slaveCPUFreq > 0) ? (guint64)slaveCPUFreq : 0;
            if(params->cpuFrequency == 0) {
                params->cpuFrequency = 2500000; // 2.5 GHz
                debug("both configured and raw slave cpu frequencies unavailable, using 2500000 KHz");
            }

            gint defaultCPUThreshold = options_getCPUThreshold(master->options);
            params->cpuThreshold = defaultCPUThreshold > 0 ? defaultCPUThreshold : 0;
            gint defaultCPUPrecision = options_getCPUPrecision(master->options);
            params->cpuPrecision = defaultCPUPrecision > 0 ? defaultCPUPrecision : 0;

            params->logLevel = ne->loglevel.isSet ?
                    loglevel_fromStr(ne->loglevel.string->str) :
                    options_getLogLevel(master->options);

            params->heartbeatLogLevel = ne->heartbeatloglevel.isSet ?
                    loglevel_fromStr(ne->heartbeatloglevel.string->str) :
                    options_getHeartbeatLogLevel(master->options);

            params->heartbeatInterval = ne->heartbeatfrequency.isSet ?
                    (SimulationTime)(ne->heartbeatfrequency.integer * SIMTIME_ONE_SECOND) :
                    options_getHeartbeatInterval(master->options);

            params->heartbeatLogInfo = ne->heartbeatloginfo.isSet ?
                    options_toHeartbeatLogInfo(master->options, ne->heartbeatloginfo.string->str) :
                    options_getHeartbeatLogInfo(master->options);

            params->logPcap = (ne->logpcap.isSet && !g_ascii_strcasecmp(ne->logpcap.string->str, "true")) ? TRUE : FALSE;
            params->pcapDir = ne->pcapdir.isSet ? ne->pcapdir.string->str : NULL;

            /* socket buffer settings - if size is set manually, turn off autotuning */
            params->recvBufSize = ne->socketrecvbuffer.isSet ? ne->socketrecvbuffer.integer :
                    options_getSocketReceiveBufferSize(master->options);
            params->autotuneRecvBuf = ne->socketrecvbuffer.isSet ? FALSE :
                    options_doAutotuneReceiveBuffer(master->options);

            params->sendBufSize = ne->socketsendbuffer.isSet ? ne->socketsendbuffer.integer :
                    options_getSocketSendBufferSize(master->options);
            params->autotuneSendBuf = ne->socketsendbuffer.isSet ? FALSE :
                    options_doAutotuneSendBuffer(master->options);

            params->interfaceBufSize = ne->interfacebuffer.isSet ? ne->interfacebuffer.integer :
                    options_getInterfaceBufferSize(master->options);
            params->qdisc = options_getQueuingDiscipline(master->options);

            slave_addNewVirtualHost(master->slave, params);

            /* now handle each virtual process the host will run */
            GList* processes = ne->applications;
            GList* processItem = processes;
            while(processItem != NULL) {
                ConfigurationApplicationElement* pe = processItem->data;
                utility_assert(pe->plugin.isSet && pe->plugin.string);
                utility_assert(pe->arguments.isSet && pe->arguments.string);

                slave_addNewVirtualProcess(master->slave, params->hostname, pe->plugin.string->str,
                                    SIMTIME_ONE_SECOND * pe->starttime.integer,
                                    pe->stoptime.isSet ? SIMTIME_ONE_SECOND * pe->stoptime.integer : 0,
                                    pe->arguments.string->str);

                processItem = g_list_next(processItem);
            }

            /* cleanup for next pass through the loop */
            g_string_free(hostnameBuffer, TRUE);
            g_free(params);
        }

        nodeItem = g_list_next(nodeItem);
    }
}

gint master_run(Master* master) {
    MAGIC_ASSERT(master);

    message("loading and initializing simulation data");

    /* start loading and initializing simulation data */
    _master_loadConfiguration(master);
    _master_loadTopology(master);
    _master_initializeTimeWindows(master);

    /* the master will be responsible for distributing the actions to the slaves so that
     * they all have a consistent view of the simulation, topology, etc.
     * For now we only have one slave so send it everything. */
    guint slaveSeed = random_nextUInt(master->random);
    master->slave = slave_new(master, master->options, master->endTime, slaveSeed);

    message("registering plugins and hosts");

    /* register the components needed by each slave.
     * this must be done after slaves are available so we can send them messages */
    _master_registerPlugins(master);
    _master_registerHosts(master);

    message("running simulation");

    /* start running each slave */
    slave_run(master->slave);

    message("simulation finished, cleaning up now");

    return slave_free(master->slave);
}

gboolean master_slaveFinishedCurrentRound(Master* master, SimulationTime minNextEventTime,
        SimulationTime* executeWindowStart, SimulationTime* executeWindowEnd) {
    MAGIC_ASSERT(master);
    utility_assert(executeWindowStart && executeWindowEnd);

    /* TODO: once we get multiple slaves, we have to block them here
     * until they have all notified us that they are finished */

    /* update our detected min jump time */
    master->minJumpTime = master->nextMinJumpTime;

    /* update the next interval window based on next event times */
    SimulationTime newStart = minNextEventTime;
    SimulationTime newEnd = minNextEventTime + _master_getMinTimeJump(master);

    /* update the new window end as one interval past the new window start,
     * making sure we dont run over the experiment end time */
    if(newEnd > master->endTime) {
        newEnd = master->endTime;
    }

    /* finally, set the new values */
    master->executeWindowStart = newStart;
    master->executeWindowEnd = newEnd;

    *executeWindowStart = master->executeWindowStart;
    *executeWindowEnd = master->executeWindowEnd;

    /* return TRUE if we should keep running */
    return newStart < newEnd ? TRUE : FALSE;
}

gdouble master_getLatency(Master* master, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(master);
    return topology_getLatency(master->topology, srcAddress, dstAddress);
}

DNS* master_getDNS(Master* master) {
    MAGIC_ASSERT(master);
    return master->dns;
}

Topology* master_getTopology(Master* master) {
    MAGIC_ASSERT(master);
    return master->topology;
}
