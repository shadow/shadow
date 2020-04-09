/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_LOGGER_HELPER_H_
#define SHD_LOGGER_HELPER_H_

#include <glib.h>

#include "utility/shd-count-down-latch.h"

typedef enum _LoggerHelperCommmandType LoggerHelperCommmandType;
enum _LoggerHelperCommmandType {
    LHC_STOP, LHC_REGISTER, LHC_FLUSH,
};

typedef struct _LoggerHelperCommand LoggerHelperCommand;

LoggerHelperCommand* loggerhelpercommand_new(LoggerHelperCommmandType type, gpointer argument);
void loggerhelpercommand_ref(LoggerHelperCommand* command);
void loggerhelpercommand_unref(LoggerHelperCommand* command);

typedef struct _LoggerHelperRunData LoggerHelperRunData;
struct _LoggerHelperRunData {
    GAsyncQueue* commands;
    CountDownLatch* notifyDoneRunning;
};

gpointer loggerhelper_runHelperThread(LoggerHelperRunData* data);

#endif /* SHD_LOGGER_HELPER_H_ */
