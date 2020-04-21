/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/core/logger/logger_helper.h"

#include <stddef.h>

#include "main/core/logger/log_record.h"
#include "main/core/support/definitions.h"
#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"

struct _LoggerHelperCommand {
    LoggerHelperCommmandType type;
    gpointer argument;
    gint referenceCount;
    MAGIC_DECLARE;
};

LoggerHelperCommand* loggerhelpercommand_new(LoggerHelperCommmandType type, gpointer argument) {
    LoggerHelperCommand* command = g_new0(LoggerHelperCommand, 1);
    MAGIC_INIT(command);
    command->type = type;
    command->argument = argument;
    command->referenceCount = 1;
    return command;
}

static void _loggerhelpercommand_free(LoggerHelperCommand* command) {
    MAGIC_CLEAR(command);
    g_free(command);
}

void loggerhelpercommand_ref(LoggerHelperCommand* command) {
    MAGIC_ASSERT(command);
    command->referenceCount++;
}

void loggerhelpercommand_unref(LoggerHelperCommand* command) {
    MAGIC_ASSERT(command);
    command->referenceCount--;
    gboolean shouldFree = (command->referenceCount <= 0) ? TRUE : FALSE;
    if(shouldFree) {
        _loggerhelpercommand_free(command);
    }
}

static void _loggerhelper_sort(GAsyncQueue* incomingRecords, PriorityQueue* sortedRecords) {
    if(incomingRecords == NULL || sortedRecords == NULL) {
        return;
    }

    GQueue* records = NULL;
    while((records = g_async_queue_try_pop(incomingRecords)) != NULL) {
        while(!g_queue_is_empty(records)) {
            LogRecord* record = g_queue_pop_head(records);
            if(record != NULL) {
                priorityqueue_push(sortedRecords, record);
            }
        }
        g_queue_free(records);
    }
}

gpointer loggerhelper_runHelperThread(LoggerHelperRunData* data) {
    GAsyncQueue* commands = data->commands;
    CountDownLatch* notifyDoneRunning = data->notifyDoneRunning;
    g_free(data);
    data = NULL;

    GQueue* queues = g_queue_new();
    PriorityQueue* sortedRecords = priorityqueue_new((GCompareDataFunc)logrecord_compare, NULL, NULL);

    LoggerHelperCommand* command = NULL;
    gboolean stop = FALSE;

    while(!stop && (command = g_async_queue_pop(commands)) != NULL) {
        MAGIC_ASSERT(command);
        switch(command->type) {
            case LHC_REGISTER: {
                GAsyncQueue* incomingRecords = command->argument;
                g_queue_push_tail(queues, incomingRecords);
                break;
            }

            case LHC_FLUSH: {
                g_queue_foreach(queues, (GFunc)_loggerhelper_sort, sortedRecords);
                while(!priorityqueue_isEmpty(sortedRecords)) {
                    LogRecord* record = priorityqueue_pop(sortedRecords);
                    gchar* logRecordStr = logrecord_toString(record);
                    utility_assert(logRecordStr);
                    g_print("%s", logRecordStr);
                    g_free(logRecordStr);
                    logrecord_unref(record);
                }
                utility_assert(priorityqueue_isEmpty(sortedRecords));
                break;
            }

            case LHC_STOP: {
                stop = TRUE;
                break;
            }

            default:
                break;
        }

        loggerhelpercommand_unref(command);
    }

    while(!g_queue_is_empty(queues)) {
        g_async_queue_unref(g_queue_pop_head(queues));
    }
    g_queue_free(queues);
    priorityqueue_free(sortedRecords);

    countdownlatch_countDown(notifyDoneRunning);
    return NULL;
}
