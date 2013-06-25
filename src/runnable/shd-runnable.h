/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_RUNNABLE_H_
#define SHD_RUNNABLE_H_

#include "shadow.h"

typedef struct _Runnable Runnable;
typedef struct _RunnableFunctionTable RunnableFunctionTable;

/* required interface functions. _new() should be implemented in subclass. */
typedef void (*RunnableRunFunc)(Runnable* r);
typedef void (*RunnableFreeFunc)(Runnable* r);

/**
 * Virtual function table for base runnable, storing pointers to required
 * callable functions.
 */
struct _RunnableFunctionTable {
	RunnableRunFunc run;
	RunnableFreeFunc free;
	MAGIC_DECLARE;
};

/**
 * A base event and its members. Subclasses extend Event by keeping this as
 * the first element in the substructure, and adding custom members below it.
 */
struct _Runnable {
	RunnableFunctionTable* vtable;
	MAGIC_DECLARE;
};

/**
 *
 */
void runnable_init(Runnable* r, RunnableFunctionTable* vtable);

/**
 *
 */
void runnable_run(gpointer data);

/**
 *
 */
void runnable_free(gpointer data);

#endif /* SHD_RUNNABLE_H_ */
