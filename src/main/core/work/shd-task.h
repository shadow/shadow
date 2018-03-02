/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TASK_H_
#define SHD_TASK_H_

typedef void (*TaskCallbackFunc)(gpointer callbackObject, gpointer callbackArgument);
typedef void (*TaskObjectFreeFunc)(gpointer data);
typedef void (*TaskArgumentFreeFunc)(gpointer data);

typedef struct _Task Task;

Task* task_new(TaskCallbackFunc callback, gpointer callbackObject, gpointer callbackArgument,
        TaskObjectFreeFunc objectFree, TaskArgumentFreeFunc argumentFree);
void task_ref(Task* task);
void task_unref(Task* task);
void task_execute(Task* task);

#endif /* SHD_TASK_H_ */
