/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROGRAM_H_
#define SHD_PROGRAM_H_

#include "shadow.h"

typedef struct _Program Program;

Program* program_new(const gchar* name, const gchar* path);
void program_free(Program* prog);

void program_load(Program* prog);
void program_unload(Program* prog);

void program_setExecuting(Program* prog, gboolean isExecuting);
const gchar* program_getName(Program* prog);
const gchar* program_getPath(Program* prog);

gint program_callMainFunc(Program* prog, gchar** argv, gint argc);
void program_callPreProcessEnterHookFunc(Program* prog);
void program_callPostProcessExitHookFunc(Program* prog);

#endif /* SHD_PROGRAM_H_ */
