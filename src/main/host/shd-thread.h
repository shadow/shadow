/*
 * shd-thread.h
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_THREAD_H_
#define SRC_MAIN_HOST_SHD_THREAD_H_

#include <glib.h>

typedef struct _Thread Thread;

Thread* thread_new();
void thread_ref(Thread* thread);
void thread_unref(Thread* thread);

void thread_start(Thread* thread, int argc, char** argv);
void thread_continue(Thread* thread);
int thread_stop(Thread* thread);

gboolean thread_isAlive(Thread* thread);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
