/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/* these are only avail in glib >= 2.30, needed for signals */
#include <glib-unix.h>
#include <signal.h>

#include "shadow.h"

struct _Master {
    /* general configuration options for the simulation */
    Configuration* config;

    /* tracks overall wall-clock runtime */
    GTimer* runTimer;

    /* global random source from which all node random sources originate */
    Random* random;

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

    /* TRUE if the engine is no longer running events and is in cleanup mode */
    gboolean killed;

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

Master* master_new(Configuration* config) {
    MAGIC_ASSERT(config);

    /* Don't do anything in this function that will cause a log message. The
     * global engine is still NULL since we are creating it now, and logging
     * here will cause an assertion error.
     */

    Master* master = g_new0(Master, 1);
    MAGIC_INIT(master);

    master->config = config;
    master->random = random_new(config->randomSeed);
    master->runTimer = g_timer_new();

    master->minJumpTimeConfig = ((SimulationTime)config->minRunAhead) * SIMTIME_ONE_MILLISECOND;

    /* these are only avail in glib >= 2.30
     * setup signal handlers for gracefully handling shutdowns */
//  TODO
//  g_unix_signal_add(SIGTERM, (GSourceFunc)_master_handleInterruptSignal, master);
//  g_unix_signal_add(SIGHUP, (GSourceFunc)_master_handleInterruptSignal, master);
//  g_unix_signal_add(SIGINT, (GSourceFunc)_master_handleInterruptSignal, master);

    return master;
}

void master_free(Master* master) {
    MAGIC_ASSERT(master);

    /* engine is now killed */
    master->killed = TRUE;

    GDateTime* dt_now = g_date_time_new_now_local();
    gchar* dt_format = g_date_time_format(dt_now, "%F %H:%M:%S");
    message("%s shut down cleanly at %s", SHADOW_VERSION_STRING, dt_format);
    g_date_time_unref(dt_now);
    g_free(dt_format);

    random_free(master->random);

    MAGIC_CLEAR(master);
    g_free(master);
}

SimulationTime master_getMinTimeJump(Master* master) {
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

SimulationTime master_getExecutionBarrier(Master* master) {
    MAGIC_ASSERT(master);
    return master->executeWindowEnd;
}

gint master_run(Master* master) {
    MAGIC_ASSERT(master);

    guint slaveSeed = (guint)random_nextInt(master->random);
    Slave* slave = slave_new(master, master->config, slaveSeed);

    /* hook in our logging system. stack variable used to avoid errors
     * during cleanup below. */
    GLogLevelFlags configuredLogLevel = configuration_getLogLevel(master->config);
    g_log_set_default_handler(logging_handleLog, &(configuredLogLevel));

    /* start off with some status messages */
    message(SHADOW_VERSION_STRING);
    message(SHADOW_INFO_STRING);
    gint igraphMajor = -1, igraphMinor = -1, igraphPatch = -1;
#if defined(IGRAPH_VERSION)
    igraph_version(NULL, &igraphMajor, &igraphMinor, &igraphPatch);
#endif
    GDateTime* dt_now = g_date_time_new_now_local();
    gchar* dt_format = g_date_time_format(dt_now, "%F %H:%M:%S");
    message("Shadow initialized at %s using GLib v%u.%u.%u and IGraph v%i.%i.%i",
        dt_format, (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
        igraphMajor, igraphMinor, igraphPatch);
    g_date_time_unref(dt_now);
    g_free(dt_format);
    message("args=%s", master->config->argstr);
    message("LD_PRELOAD=%s", g_getenv("LD_PRELOAD"));
    message("SHADOW_SPAWNED=%s", g_getenv("SHADOW_SPAWNED"));

    if(master->config->debug) {
        message("Pausing with SIGTSTP to enable debugger attachment (pid %i)", (gint)getpid());
        g_printerr("** Pausing with SIGTSTP to enable debugger attachment (pid %i)\n", (gint)getpid());
        raise(SIGTSTP);
    }

    /* store parsed actions from each user-configured simulation script  */
    GQueue* actions = g_queue_new();
    Parser* xmlParser = parser_new();

    /* parse built-in examples, or input files */
    gboolean success = TRUE;
    if(FALSE){//master->config->runTGenExample) {
        GString* file = example_getFileExampleContents();
        success = parser_parseContents(xmlParser, file->str, file->len, actions);
        g_string_free(file, TRUE);
    } else {
        /* parse all given input XML files */
        while(success && g_queue_get_length(master->config->inputXMLFilenames) > 0) {
            GString* filename = g_queue_pop_head(master->config->inputXMLFilenames);
            success = parser_parseFile(xmlParser, filename, actions);
        }
    }

    parser_free(xmlParser);

    /* if there was an error parsing, bounce out */
    if(success) {
        message("successfully parsed Shadow XML input!");
    } else {
        g_queue_free(actions);
        error("error parsing Shadow XML input!");
    }

    /*
     * loop through actions that were created from parsing. this will create
     * all the nodes, networks, applications, etc., and add an application
     * start event for each node to bootstrap the simulation. Note that the
     * plug-in libraries themselves are not loaded until a worker needs it,
     * since each worker will need its own private version.
     */
    while(g_queue_get_length(actions) > 0) {
        Action* a = g_queue_pop_head(actions);
        runnable_run(a);
        runnable_free(a);
    }
    g_queue_free(actions);

    /* start running */
    gint nWorkers = configuration_getNWorkerThreads(master->config);
    debug("starting %i-threaded engine (main + %i workers)", (nWorkers + 1), nWorkers);

    /* simulation mode depends on configured number of workers */
    if(nWorkers > 0) {
        /* multi threaded, manage the other workers */
        master->executeWindowStart = 0;
        SimulationTime jump = master_getMinTimeJump(master);
        master->executeWindowEnd = jump;
        master->nextMinJumpTime = jump;
        slave_runParallel(slave);
    } else {
        /* single threaded, we are the only worker */
        master->executeWindowStart = 0;
        master->executeWindowEnd = G_MAXUINT64;
        slave_runSerial(slave);
    }

    debug("engine finished, cleaning up...");

    return slave_free(slave);
}

GTimer* master_getRunTimer(Master* master) {
    MAGIC_ASSERT(master);
    return master->runTimer;
}

void master_setKillTime(Master* master, SimulationTime endTime) {
    MAGIC_ASSERT(master);
    master->endTime = endTime;
}

gboolean master_isKilled(Master* master) {
    MAGIC_ASSERT(master);
    return master->killed;
}

void master_setKilled(Master* master, gboolean isKilled) {
    MAGIC_ASSERT(master);
    master->killed = isKilled;
}

SimulationTime master_getExecuteWindowEnd(Master* master) {
    MAGIC_ASSERT(master);
    return master->executeWindowEnd;
}

SimulationTime master_getExecuteWindowStart(Master* master) {
    MAGIC_ASSERT(master);
    return master->executeWindowStart;
}

SimulationTime master_getEndTime(Master* master) {
    MAGIC_ASSERT(master);
    return master->endTime;
}

void master_slaveFinishedCurrentWindow(Master* master, SimulationTime minNextEventTime) {
    MAGIC_ASSERT(master);
    utility_assert(minNextEventTime != SIMTIME_INVALID);

    /* TODO: once we get multiple slaves, we have to block them here
     * until they have all notified us that they are finished */

    /* update our detected min jump time */
    master->minJumpTime = master->nextMinJumpTime;

    /* update the next interval window based on next event times */
    SimulationTime newStart = minNextEventTime;
    SimulationTime newEnd = minNextEventTime + master_getMinTimeJump(master);

    /* update the new window end as one interval past the new window start,
     * making sure we dont run over the experiment end time */
    if(newEnd > master->endTime) {
        newEnd = master->endTime;
    }

    /* if we are done, make sure the workers know about it */
    if(newStart >= newEnd) {
        master_setKilled(master, TRUE);
    }

    /* finally, set the new values */
    master->executeWindowStart = newStart;
    master->executeWindowEnd = newEnd;
}

