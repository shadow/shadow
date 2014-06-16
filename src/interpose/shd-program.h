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

Program* program_getTemporaryCopy(Program* prog);
void program_registerResidentState(Program* prog, PluginNewInstanceFunc new,
        PluginNotifyFunc free, PluginNotifyFunc notify);

void program_swapInState(Program* prog, ProgramState state);
void program_swapOutState(Program* prog, ProgramState state);

ShadowPluginInitializeFunc program_getInitFunc(Program* prog);
PluginNewInstanceFunc program_getNewFunc(Program* prog);
PluginNotifyFunc program_getFreeFunc(Program* prog);
PluginNotifyFunc program_getNotifyFunc(Program* prog);

ProgramState program_newDefaultState(Program* prog);
void program_freeState(Program* prog, gpointer state);

GQuark* program_getID(Program* prog);
gboolean program_isRegistered(Program* prog);
const gchar* program_getName(Program* prog);

#endif /* SHD_PROGRAM_H_ */
