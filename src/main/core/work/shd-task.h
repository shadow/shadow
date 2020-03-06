/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TASK_H_
#define SHD_TASK_H_

typedef void (*TaskCallbackFunc)(gpointer callbackObject, gpointer callbackArgument);
typedef void (*TaskObjectFreeFunc)(gpointer data);
typedef void (*TaskArgumentFreeFunc)(gpointer data);

/* An event for the currently active host, i.e.,
 * the same host as the event initiator and running on the same slave machine.
 * (These are non-packet events for localhost.) */
typedef struct _Task Task;

Task* task_new(TaskCallbackFunc callback, gpointer callbackObject, gpointer callbackArgument,
        TaskObjectFreeFunc objectFree, TaskArgumentFreeFunc argumentFree);
void task_ref(Task* task);
void task_unref(Task* task);
void task_execute(Task* task);

#endif /* SHD_TASK_H_ */
