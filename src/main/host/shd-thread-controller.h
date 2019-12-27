/*
 * shd-thread-controller.h
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_THREAD_CONTROLLER_H_
#define SRC_MAIN_HOST_SHD_THREAD_CONTROLLER_H_

#include <glib.h>

typedef struct _ThreadControlBlock ThreadControlBlock;

ThreadControlBlock* threadcontroller_new();
void threadcontroller_ref(ThreadControlBlock* tcb);
void threadcontroller_unref(ThreadControlBlock* tcb);

void threadcontroller_start(ThreadControlBlock* tcb, int argc, char** argv);
void threadcontroller_continue(ThreadControlBlock* tcb);
int threadcontroller_stop(ThreadControlBlock* tcb);

gboolean threadcontroller_isAlive(ThreadControlBlock* tcb);

#endif /* SRC_MAIN_HOST_SHD_THREAD_CONTROLLER_H_ */
