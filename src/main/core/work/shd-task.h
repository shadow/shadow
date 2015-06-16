/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TASK_H_
#define SHD_TASK_H_

typedef void (*TaskFunc)(gpointer data, gpointer callbackArgument);

typedef struct _Task Task;

Task* task_new(TaskFunc callback, gpointer data, gpointer callbackArgument);
void task_ref(Task* task);
void task_unref(Task* task);
void task_execute(Task* task);

#endif /* SHD_TASK_H_ */
