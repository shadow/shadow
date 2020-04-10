/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "core/support/shd-options.h"

#include <stddef.h>

#include "core/logger/shd-logger.h"
#include "utility/shd-utility.h"

struct _Options {
    GOptionContext *context;

    gchar* argstr;

    GOptionGroup* mainOptionGroup;
    gchar* logLevelInput;
    gint nWorkerThreads;
    guint randomSeed;
    gboolean printSoftwareVersion;
    guint heartbeatInterval;
    gchar* heartbeatLogLevelInput;
    gchar* heartbeatLogInfo;
    gchar* preloads;
    gboolean runValgrind;
    gboolean debug;
    gchar* dataDirPath;
    gchar* dataTemplatePath;

    GOptionGroup* networkOptionGroup;
    gint cpuThreshold;
    gint cpuPrecision;
    gint minRunAhead;
    gint initialTCPWindow;
    gint interfaceBufferSize;
    gint initialSocketReceiveBufferSize;
    gint initialSocketSendBufferSize;
    gboolean autotuneSocketReceiveBuffer;
    gboolean autotuneSocketSendBuffer;
    gchar* interfaceQueuingDiscipline;
    gchar* eventSchedulingPolicy;
    SimulationTime interfaceBatchTime;
    gchar* tcpCongestionControl;
    gint tcpSlowStartThreshold;

    GOptionGroup* pluginsOptionGroup;
    gboolean runTGenExample;
    gboolean runTestExample;

    GString* inputXMLFilename;

    MAGIC_DECLARE;
};

Options* options_new(gint argc, gchar* argv[]) {
    /* get memory */
    Options* options = g_new0(Options, 1);
    MAGIC_INIT(options);

    options->argstr = g_strjoinv(" ", argv);

    const gchar* required_parameters = "shadow.config.xml";
    gint nRequiredXMLFiles = 1;

    options->context = g_option_context_new(required_parameters);
    g_option_context_set_summary(options->context, "Shadow - run real applications over simulated networks");
    g_option_context_set_description(options->context,
        "Shadow is a unique discrete-event network simulator that runs real "
        "applications like Tor, and distributed systems of thousands of nodes "
        "on a single machine. Shadow combines the accuracy of emulation with the "
        "efficiency and control of simulation, achieving the best of both approaches.");

    /* set defaults */
    options->initialTCPWindow = 10;
    options->interfaceBufferSize = 1024000;
    options->interfaceBatchTime = 5000;
    options->randomSeed = 1;
    options->cpuThreshold = -1;
    options->cpuPrecision = 200;
    options->heartbeatInterval = 1;

    /* set options to change defaults for the main group */
    options->mainOptionGroup = g_option_group_new("main", "Main Options", "Primary simulator options", NULL, NULL);
    const GOptionEntry mainEntries[] = {
      { "data-directory", 'd', 0, G_OPTION_ARG_STRING, &(options->dataDirPath), "PATH to store simulation output ['shadow.data']", "PATH" },
      { "data-template", 'e', 0, G_OPTION_ARG_STRING, &(options->dataTemplatePath), "PATH to recursively copy during startup and use as the data-directory ['shadow.data.template']", "PATH" },
      { "gdb", 'g', 0, G_OPTION_ARG_NONE, &(options->debug), "Pause at startup for debugger attachment", NULL },
      { "heartbeat-frequency", 'h', 0, G_OPTION_ARG_INT, &(options->heartbeatInterval), "Log node statistics every N seconds [1]", "N" },
      { "heartbeat-log-info", 'i', 0, G_OPTION_ARG_STRING, &(options->heartbeatLogInfo), "Comma separated list of information contained in heartbeat ('node','socket','ram') ['node']", "LIST"},
      { "heartbeat-log-level", 'j', 0, G_OPTION_ARG_STRING, &(options->heartbeatLogLevelInput), "Log LEVEL at which to print node statistics ['message']", "LEVEL" },
      { "log-level", 'l', 0, G_OPTION_ARG_STRING, &(options->logLevelInput), "Log LEVEL above which to filter messages ('error' < 'critical' < 'warning' < 'message' < 'info' < 'debug') ['message']", "LEVEL" },
      { "preload", 'p', 0, G_OPTION_ARG_STRING, &(options->preloads), "LD_PRELOAD environment VALUE to use for function interposition (/path/to/lib:...) [None]", "VALUE" },
      { "runahead", 'r', 0, G_OPTION_ARG_INT, &(options->minRunAhead), "If set, overrides the automatically calculated minimum TIME workers may run ahead when sending events between nodes, in milliseconds [0]", "TIME" },
      { "seed", 's', 0, G_OPTION_ARG_INT, &(options->randomSeed), "Initialize randomness for each thread using seed N [1]", "N" },
      { "scheduler-policy", 't', 0, G_OPTION_ARG_STRING, &(options->eventSchedulingPolicy), "The event scheduler's policy for thread synchronization ('thread', 'host', 'steal', 'threadXthread', 'threadXhost') ['steal']", "SPOL" },
      { "workers", 'w', 0, G_OPTION_ARG_INT, &(options->nWorkerThreads), "Run concurrently with N worker threads [0]", "N" },
      { "valgrind", 'x', 0, G_OPTION_ARG_NONE, &(options->runValgrind), "Run through valgrind for debugging", NULL },
      { "version", 'v', 0, G_OPTION_ARG_NONE, &(options->printSoftwareVersion), "Print software version and exit", NULL },
      { NULL },
    };

    g_option_group_add_entries(options->mainOptionGroup, mainEntries);
    g_option_context_set_main_group(options->context, options->mainOptionGroup);

    /* now fill in the default plug-in examples option group */
    options->pluginsOptionGroup = g_option_group_new("sim", "Simulation Examples", "Built-in simulation examples", NULL, NULL);
    const GOptionEntry pluginEntries[] =
    {
      { "test", 0, 0, G_OPTION_ARG_NONE, &(options->runTestExample), "Run basic benchmark tests", NULL },
      { "tgen", 0, 0, G_OPTION_ARG_NONE, &(options->runTGenExample), "PLACEHOLDER - Run basic data transfer simulation", NULL },
      { NULL },
    };

    g_option_group_add_entries(options->pluginsOptionGroup, pluginEntries);
    g_option_context_add_group(options->context, options->pluginsOptionGroup);

    /* now fill in the network option group */
    GString* sockrecv = g_string_new("");
    g_string_printf(sockrecv, "Initialize the socket receive buffer to N bytes [%i]", (gint)CONFIG_RECV_BUFFER_SIZE);
    GString* socksend = g_string_new("");
    g_string_printf(socksend, "Initialize the socket send buffer to N bytes [%i]", (gint)CONFIG_SEND_BUFFER_SIZE);

    options->networkOptionGroup = g_option_group_new("sys", "System Options", "Simulated system/network behavior", NULL, NULL);
    const GOptionEntry networkEntries[] =
    {
      { "cpu-precision", 0, 0, G_OPTION_ARG_INT, &(options->cpuPrecision), "round measured CPU delays to the nearest TIME, in microseconds (negative value to disable fuzzy CPU delays) [200]", "TIME" },
      { "cpu-threshold", 0, 0, G_OPTION_ARG_INT, &(options->cpuThreshold), "TIME delay threshold after which the CPU becomes blocked, in microseconds (negative value to disable CPU delays) (experimental!) [-1]", "TIME" },
      { "interface-batch", 0, 0, G_OPTION_ARG_INT, &(options->interfaceBatchTime), "Batch TIME for network interface sends and receives, in microseconds [5000]", "TIME" },
      { "interface-buffer", 0, 0, G_OPTION_ARG_INT, &(options->interfaceBufferSize), "Size of the network interface receive buffer, in bytes [1024000]", "N" },
      { "interface-qdisc", 0, 0, G_OPTION_ARG_STRING, &(options->interfaceQueuingDiscipline), "The interface queuing discipline QDISC used to select the next sendable socket ('fifo' or 'rr') ['fifo']", "QDISC" },
      { "socket-recv-buffer", 0, 0, G_OPTION_ARG_INT, &(options->initialSocketReceiveBufferSize), sockrecv->str, "N" },
      { "socket-send-buffer", 0, 0, G_OPTION_ARG_INT, &(options->initialSocketSendBufferSize), socksend->str, "N" },
      { "tcp-congestion-control", 0, 0, G_OPTION_ARG_STRING, &(options->tcpCongestionControl), "Congestion control algorithm to use for TCP ('aimd', 'reno', 'cubic') ['reno']", "TCPCC" },
      { "tcp-ssthresh", 0, 0, G_OPTION_ARG_INT, &(options->tcpSlowStartThreshold), "Set TCP ssthresh value instead of discovering it via packet loss or hystart [0]", "N" },
      { "tcp-windows", 0, 0, G_OPTION_ARG_INT, &(options->initialTCPWindow), "Initialize the TCP send, receive, and congestion windows to N packets [10]", "N" },
      { NULL },
    };

    g_option_group_add_entries(options->networkOptionGroup, networkEntries);
    g_option_context_add_group(options->context, options->networkOptionGroup);

    /* parse args */
    GError *error = NULL;
    if (!g_option_context_parse(options->context, &argc, &argv, &error)) {
        g_printerr("** %s **\n", error->message);
        gchar* helpString = g_option_context_get_help(options->context, TRUE, NULL);
        g_printerr("%s", helpString);
        g_free(helpString);
        options_free(options);
        return NULL;
    }

    /* make sure we have the required arguments. program name is first arg.
     * printing the software version requires no other args. running a
     * plug-in example also requires no other args. */
    if(!(options->printSoftwareVersion) && !(options->runTGenExample) &&
            !(options->runTestExample) && (argc != nRequiredXMLFiles + 1)) {
        g_printerr("** Please provide the required parameters **\n");
        gchar* helpString = g_option_context_get_help(options->context, TRUE, NULL);
        g_printerr("%s", helpString);
        g_free(helpString);
        options_free(options);
        return NULL;
    }

    if(options->nWorkerThreads < 0) {
        options->nWorkerThreads = 0;
    }
    if(options->logLevelInput == NULL) {
        options->logLevelInput = g_strdup("message");
    }
    if(options->heartbeatLogLevelInput == NULL) {
        options->heartbeatLogLevelInput = g_strdup("message");
    }
    if(options->heartbeatLogInfo == NULL) {
        options->heartbeatLogInfo = g_strdup("node");
    }
    if(options->heartbeatInterval < 1) {
        options->heartbeatInterval = 1;
    }
    if(options->initialTCPWindow < 1) {
        options->initialTCPWindow = 1;
    }
    if(options->interfaceBufferSize < CONFIG_MTU) {
        options->interfaceBufferSize = CONFIG_MTU;
    }
    options->interfaceBatchTime *= SIMTIME_ONE_MICROSECOND;
    if(options->interfaceBatchTime == 0) {
        /* we require at least 1 nanosecond b/c of time granularity */
        options->interfaceBatchTime = 1;
    }
    if(options->interfaceQueuingDiscipline == NULL) {
        options->interfaceQueuingDiscipline = g_strdup("fifo");
    }
    if(options->eventSchedulingPolicy == NULL) {
        options->eventSchedulingPolicy = g_strdup("steal");
    }
    if(!options->initialSocketReceiveBufferSize) {
        options->initialSocketReceiveBufferSize = CONFIG_RECV_BUFFER_SIZE;
        options->autotuneSocketReceiveBuffer = TRUE;
    }
    if(!options->initialSocketSendBufferSize) {
        options->initialSocketSendBufferSize = CONFIG_SEND_BUFFER_SIZE;
        options->autotuneSocketSendBuffer = TRUE;
    }
    if(options->tcpCongestionControl == NULL) {
        options->tcpCongestionControl = g_strdup("reno");
    }
    if(options->dataDirPath == NULL) {
        options->dataDirPath = g_strdup("shadow.data");
    }
    if(options->dataTemplatePath == NULL) {
        options->dataTemplatePath = g_strdup("shadow.data.template");
    }

    options->inputXMLFilename = g_string_new(argv[1]);

    if(socksend) {
        g_string_free(socksend, TRUE);
    }
    if(sockrecv) {
        g_string_free(sockrecv, TRUE);
    }

    return options;
}

void options_free(Options* options) {
    MAGIC_ASSERT(options);

    if(options->inputXMLFilename) {
        g_string_free(options->inputXMLFilename, TRUE);
    }
    g_free(options->logLevelInput);
    g_free(options->heartbeatLogLevelInput);
    g_free(options->heartbeatLogInfo);
    g_free(options->interfaceQueuingDiscipline);
    g_free(options->eventSchedulingPolicy);
    g_free(options->tcpCongestionControl);
    if(options->argstr) {
        g_free(options->argstr);
    }
    if(options->preloads) {
        g_free(options->preloads);
    }
    if(options->dataDirPath != NULL) {
        g_free(options->dataDirPath);
    }
    if(options->dataTemplatePath != NULL) {
        g_free(options->dataTemplatePath);
    }

    /* groups are freed with the context */
    g_option_context_free(options->context);

    MAGIC_CLEAR(options);
    g_free(options);
}

LogLevel options_getLogLevel(Options* options) {
    MAGIC_ASSERT(options);
    return loglevel_fromStr(options->logLevelInput);
}

LogLevel options_getHeartbeatLogLevel(Options* options) {
    MAGIC_ASSERT(options);
    const gchar* l = (const gchar*) options->heartbeatLogLevelInput;
    return loglevel_fromStr(l);
}

SimulationTime options_getHeartbeatInterval(Options* options) {
    MAGIC_ASSERT(options);
    return options->heartbeatInterval * SIMTIME_ONE_SECOND;
}

LogInfoFlags options_toHeartbeatLogInfo(Options* options, const gchar* input) {
    LogInfoFlags flags = LOG_INFO_FLAGS_NONE;
    if(input) {
        /* info string can either be comma or space separated */
        gchar** parts = g_strsplit_set(input, " ,", -1);
        for(gint i = 0; parts[i]; i++) {
            if(!g_ascii_strcasecmp(parts[i], "node")) {
                flags |= LOG_INFO_FLAGS_NODE;
            } else if(!g_ascii_strcasecmp(parts[i], "socket")) {
                flags |= LOG_INFO_FLAGS_SOCKET;
            } else if(!g_ascii_strcasecmp(parts[i], "ram")) {
                flags |= LOG_INFO_FLAGS_RAM;
            } else {
                warning("Did not recognize log info '%s', possible choices are 'node','socket','ram'.", parts[i]);
            }
        }
        g_strfreev(parts);
    }
    return flags;
}

LogInfoFlags options_getHeartbeatLogInfo(Options* options) {
    MAGIC_ASSERT(options);
    return options_toHeartbeatLogInfo(options, options->heartbeatLogInfo);
}

QDiscMode options_getQueuingDiscipline(Options* options) {
    MAGIC_ASSERT(options);

    if(options->interfaceQueuingDiscipline) {
        if(!g_ascii_strcasecmp(options->interfaceQueuingDiscipline, "rr")) {
            return QDISC_MODE_RR;
        } else if(!g_ascii_strcasecmp(options->interfaceQueuingDiscipline, "fifo")) {
            return QDISC_MODE_FIFO;
        }
    }

    return QDISC_MODE_NONE;
}

gchar* options_getEventSchedulerPolicy(Options* options) {
    MAGIC_ASSERT(options);
    return options->eventSchedulingPolicy;
}

guint options_getNWorkerThreads(Options* options) {
    MAGIC_ASSERT(options);
    return options->nWorkerThreads > 0 ? (guint)options->nWorkerThreads : 0;
}

const gchar* options_getArgumentString(Options* options) {
    MAGIC_ASSERT(options);
    return options->argstr;
}

guint options_getRandomSeed(Options* options) {
    MAGIC_ASSERT(options);
    return options->randomSeed;
}

gboolean options_doRunPrintVersion(Options* options) {
    MAGIC_ASSERT(options);
    return options->printSoftwareVersion;
}

gboolean options_doRunValgrind(Options* options) {
    MAGIC_ASSERT(options);
    return options->runValgrind;
}

gboolean options_doRunDebug(Options* options) {
    MAGIC_ASSERT(options);
    return options->debug;
}

gboolean options_doRunTGenExample(Options* options) {
    MAGIC_ASSERT(options);
    return options->runTGenExample;
}

gboolean options_doRunTestExample(Options* options) {
    MAGIC_ASSERT(options);
    return options->runTestExample;
}

const gchar* options_getPreloadString(Options* options) {
    MAGIC_ASSERT(options);
    return options->preloads;
}

gint options_getCPUThreshold(Options* options) {
    MAGIC_ASSERT(options);
    return options->cpuThreshold;
}

gint options_getCPUPrecision(Options* options) {
    MAGIC_ASSERT(options);
    return options->cpuPrecision;
}

gint options_getMinRunAhead(Options* options) {
    MAGIC_ASSERT(options);
    return options->minRunAhead;
}

gint options_getTCPWindow(Options* options) {
    MAGIC_ASSERT(options);
    return options->initialTCPWindow;
}

const gchar* options_getTCPCongestionControl(Options* options) {
    MAGIC_ASSERT(options);
    return options->tcpCongestionControl;
}

gint options_getTCPSlowStartThreshold(Options* options) {
    MAGIC_ASSERT(options);
    return options->tcpSlowStartThreshold;
}

SimulationTime options_getInterfaceBatchTime(Options* options) {
    MAGIC_ASSERT(options);
    return options->interfaceBatchTime;
}

gint options_getInterfaceBufferSize(Options* options) {
    MAGIC_ASSERT(options);
    return options->interfaceBufferSize;
}

gint options_getSocketReceiveBufferSize(Options* options) {
    MAGIC_ASSERT(options);
    return options->initialSocketReceiveBufferSize;
}

gint options_getSocketSendBufferSize(Options* options) {
    MAGIC_ASSERT(options);
    return options->initialSocketSendBufferSize;
}

gboolean options_doAutotuneReceiveBuffer(Options* options) {
    MAGIC_ASSERT(options);
    return options->autotuneSocketReceiveBuffer;
}

gboolean options_doAutotuneSendBuffer(Options* options) {
    MAGIC_ASSERT(options);
    return options->autotuneSocketSendBuffer;
}

const GString* options_getInputXMLFilename(Options* options) {
    MAGIC_ASSERT(options);
    return options->inputXMLFilename;
}

const gchar* options_getDataOutputPath(Options* options) {
    MAGIC_ASSERT(options);
    return options->dataDirPath;
}

const gchar* options_getDataTemplatePath(Options* options) {
    MAGIC_ASSERT(options);
    return options->dataTemplatePath;
}

