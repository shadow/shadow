/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_THREAD_H_
#define SHD_THREAD_H_

#include "shadow.h"

typedef struct _Thread Thread;

Thread* thread_new();
void thread_free(Thread* thread);

#endif /* SHD_THREAD_H_ */
