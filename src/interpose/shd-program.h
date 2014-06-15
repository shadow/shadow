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
Program* program_getTemporaryCopy(Program* prog);
void program_free(Program* prog);

GQuark* program_getID(Program* prog);

ProgramState program_newDefaultState(Program* prog);
void program_freeState(Program* prog, ProgramState state);

void program_setShadowContext(Program* prog, gboolean isShadowContext);
gboolean program_isShadowContext(Program* prog);

void program_registerResidentState(Program* prog, PluginNewInstanceFunc new, PluginNotifyFunc free, PluginNotifyFunc notify);
void program_executeNew(Program* prog, ProgramState state, gint argcParam, gchar* argvParam[]);
void program_executeFree(Program* prog, ProgramState state);
void program_executeNotify(Program* prog, ProgramState state);
void program_executeGeneric(Program* prog, ProgramState state, CallbackFunc callback, gpointer data, gpointer callbackArgument);

#endif /* SHD_PROGRAM_H_ */
