/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
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
