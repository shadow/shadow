/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Task {
    TaskFunc execute;
    gpointer data;
    gpointer callbackArgument;
    gint referenceCount;
    MAGIC_DECLARE;
};


Task* task_new(TaskFunc execute, gpointer data, gpointer callbackArgument) {
    utility_assert(execute != NULL);

    Task* task = g_new0(Task, 1);

    task->execute = execute;
    task->data = data;
    task->callbackArgument = callbackArgument;
    task->referenceCount = 1;

    MAGIC_INIT(task);
    return task;
}

static void _task_free(Task* task) {
    MAGIC_CLEAR(task);
    g_free(task);
}

void task_ref(Task* task) {
    MAGIC_ASSERT(task);
    task->referenceCount++;
}

void task_unref(Task* task) {
    MAGIC_ASSERT(task);
    task->referenceCount--;
    if(task->referenceCount <= 0) {
        _task_free(task);
    }
}

void task_execute(Task* task) {
    MAGIC_ASSERT(task);
    task->execute(task->data, task->callbackArgument);
}
