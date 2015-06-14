/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CONFIGURATION_H_
#define SHD_CONFIGURATION_H_

#include <glib.h>
#include "shadow.h"

/**
 * @addtogroup Configuration
 * @{
 * Use this module to parse command line input.
 */

typedef struct _Options Options;

typedef enum _LogInfoFlags LogInfoFlags;
enum _LogInfoFlags {
    LOG_INFO_FLAGS_NONE = 0,
    LOG_INFO_FLAGS_NODE = 1<<0,
    LOG_INFO_FLAGS_SOCKET = 1<<1,
    LOG_INFO_FLAGS_RAM = 1<<2,
};

typedef enum _QDiscMode QDiscMode;
enum _QDiscMode {
    QDISC_MODE_NONE=0, QDISC_MODE_FIFO=1, QDISC_MODE_RR=2,
};

/**
 * Create a new #Configuration and parse the command line arguments given in
 * argv. Errors encountered during parsing are printed to stderr.
 *
 * @param argc the length of the argument vector in argv
 * @param argv a vector of arguments passed in from the command line while
 * launching Shadow
 *
 * @returns a new #Configuration which should be freed with configuration_free(),
 * or NULL if an error was encountered
 */
Options* options_new(gint argc, gchar* argv[]);

/**
 * Frees a previously created #Configuration
 *
 * @param config a #Configuration object created with configuration_new()
 */
void options_free(Options* options);

/**
 * Get the log level flags corresponding to the given input string. Strings are
 * compared ignoring case. If an invalid string is supplied, the default flags
 * are returned.
 *
 * @param config a #Configuration object created with configuration_new()
 * @param input the string representing the log level. Valid strings are:
 *  'error'; 'critical'; 'warning'; 'message'; 'info'; and 'debug'.
 * @return the log level parsed from the input string, or the log level
 * corresponding to 'message' if the input is invalid.
 */
GLogLevelFlags options_toLogLevel(Options* options, const gchar* input);

/**
 * Get the configured log level based on command line input.
 *
 * @param config a #Configuration object created with configuration_new()
 *
 * @returns the log level as parsed from command line input
 */
GLogLevelFlags options_getLogLevel(Options* options);
GLogLevelFlags options_getHeartbeatLogLevel(Options* options);

/**
 * Get the configured log level at which heartbeat messages are printed,
 * based on command line input.
 *
 * @param config a #Configuration object created with configuration_new()
 *
 * @returns the heartbeat log level as parsed from command line input
 */
LogInfoFlags options_toHeartbeatLogInfo(Options* options, const gchar* input);
LogInfoFlags options_getHeartbeatLogInfo(Options* options);

/**
 * Get the configured heartbeat printing interval.
 * @param config a #Configuration object created with configuration_new()
 * @return the command line heartbeat interval converted to SimulationTime
 */
SimulationTime options_getHeartbeatInterval(Options* options);

/**
 * Get the string form that represents the queuing discipline the network
 * interface uses to select which of the sendable sockets should get priority.
 * @param config a #Configuration object created with configuration_new()
 * @return the qdisc string. the caller does not own the string.
 */
QDiscMode options_getQueuingDiscipline(Options* options);

gchar* options_getEventSchedulerPolicy(Options* options);

guint options_getNWorkerThreads(Options* options);

const gchar* options_getArgumentString(Options* options);
const gchar* options_getHeartbeatLogInfoString(Options* options);
const gchar* options_getPreloadString(Options* options);
guint options_getRandomSeed(Options* options);

gboolean options_doRunPrintVersion(Options* options);
gboolean options_doRunValgrind(Options* options);
gboolean options_doRunDebug(Options* options);
gboolean options_doRunTGenExample(Options* options);

gint options_getCPUThreshold(Options* options);
gint options_getCPUPrecision(Options* options);

gint options_getMinRunAhead(Options* options);
gint options_getTCPWindow(Options* options);
const gchar* options_getTCPCongestionControl(Options* options);
gint options_getTCPSlowStartThreshold(Options* options);
SimulationTime options_getInterfaceBatchTime(Options* options);
gint options_getInterfaceBufferSize(Options* options);
gint options_getSocketReceiveBufferSize(Options* options);
gint options_getSocketSendBufferSize(Options* options);
gboolean options_doAutotuneReceiveBuffer(Options* options);
gboolean options_doAutotuneSendBuffer(Options* options);

const GString* options_getInputXMLFilename(Options* options);

/** @} */

#endif /* SHD_CONFIGURATION_H_ */
