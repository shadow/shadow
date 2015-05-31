/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_THREAD_H_
#define SHD_THREAD_H_

#include "shadow.h"

typedef struct _Thread Thread;

Thread* thread_new(Process* parentProc, Program* prog);
void thread_ref(Thread* thread);
void thread_unref(Thread* thread);

gboolean thread_isRunning(Thread* thread);
void thread_stop(Thread* thread);

void thread_executeInit(Thread* thread, ShadowPluginInitializeFunc init);
void thread_executeNew(Thread* thread, PluginNewInstanceFunc new, gint argcParam, gchar* argvParam[]);
void thread_execute(Thread* thread, PluginNotifyFunc func);
void thread_executeExitCallback(Thread* thread, void (*callback)(int , void *), gpointer argument);
void thread_executeCallback2(Thread* thread, CallbackFunc callback, gpointer data, gpointer callbackArgument);

gboolean thread_shouldInterpose(Thread* thread);
void thread_beginControl(Thread* thread);
void thread_endControl(Thread* thread);
Process* thread_getParentProcess(Thread* thread);
Program* thread_getProgram(Thread* thread);

#endif /* SHD_THREAD_H_ */
