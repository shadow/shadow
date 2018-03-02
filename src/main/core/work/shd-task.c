/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Task {
    TaskCallbackFunc execute;
    gpointer callbackObject;
    gpointer callbackArgument;
    TaskObjectFreeFunc objectFree;
    TaskArgumentFreeFunc argumentFree;
    gint referenceCount;
    MAGIC_DECLARE;
};


Task* task_new(TaskCallbackFunc callback, gpointer callbackObject, gpointer callbackArgument,
        TaskObjectFreeFunc objectFree, TaskArgumentFreeFunc argumentFree) {
    utility_assert(callback != NULL);

    Task* task = g_new0(Task, 1);

    task->execute = callback;
    task->callbackObject = callbackObject;
    task->callbackArgument = callbackArgument;
    task->objectFree = objectFree;
    task->argumentFree = argumentFree;
    task->referenceCount = 1;

    MAGIC_INIT(task);

    worker_countObject(OBJECT_TYPE_TASK, COUNTER_TYPE_NEW);
    return task;
}

static void _task_free(Task* task) {
    if(task->objectFree && task->callbackObject) {
        task->objectFree(task->callbackObject);
    }
    if(task->argumentFree && task->callbackArgument) {
        task->argumentFree(task->callbackArgument);
    }
    MAGIC_CLEAR(task);
    g_free(task);
    worker_countObject(OBJECT_TYPE_TASK, COUNTER_TYPE_FREE);
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
    task->execute(task->callbackObject, task->callbackArgument);
}
