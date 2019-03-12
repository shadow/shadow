/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_COMMAND_H_
#define SHD_COMMAND_H_

typedef struct _Command Command;

SimulationTime command_getStartTime(Command* command);
Command* command_new(gpointer host, gchar* id, SimulationTime startTime, gchar* arguments);
void command_ref(Command* task);
void command_unref(Command* task);

void command_run(Command* command, gpointer userData);

#endif /* SHD_COMMAND_H_ */
