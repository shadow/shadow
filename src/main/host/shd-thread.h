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

#include "main/host/shd-syscall-handler.h"

Thread* thread_new(gint threadID, SysCallHandler* sys);
void thread_ref(Thread* thread);
void thread_unref(Thread* thread);

void thread_run(Thread* thread, gchar** argv, gchar** envv);
void thread_resume(Thread* thread);
int thread_terminate(Thread* thread);

gboolean thread_isAlive(Thread* thread);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
