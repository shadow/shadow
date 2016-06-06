/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROGRAM_H_
#define SHD_PROGRAM_H_

#include "shadow.h"

typedef struct _Program Program;
typedef gpointer ProgramState;

Program* program_new(const gchar* name, const gchar* path);
void program_free(Program* prog);

void program_swapInState(Program* prog, ProgramState state);
void program_swapOutState(Program* prog, ProgramState state);

ProgramState program_newDefaultState(Program* prog);
void program_freeState(Program* prog, gpointer state);

Program* program_getTemporaryCopy(Program* prog);
GQuark* program_getID(Program* prog);
const gchar* program_getName(Program* prog);
const gchar* program_getPath(Program* prog);
void* program_getHandle(Program* prog);

gint program_callMainFunc(Program* prog, gchar** argv, gint argc);
void program_callPreProcessEnterHookFunc(Program* prog);
void program_callPostProcessExitHookFunc(Program* prog);

#endif /* SHD_PROGRAM_H_ */
