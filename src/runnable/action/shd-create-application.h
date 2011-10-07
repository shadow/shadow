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

#ifndef SHD_CREATE_APPLICATION_H_
#define SHD_CREATE_APPLICATION_H_

#include "shadow.h"

typedef struct _CreateApplicationAction CreateApplicationAction;

struct _CreateApplicationAction {
	Action super;
	GString* name;
	GString* pluginName;
	GString* arguments;
	SimulationTime launchtime;
	MAGIC_DECLARE;
};

CreateApplicationAction* createapplication_new(GString* name,
		GString* pluginName, GString* arguments, guint64 launchtime);
void createapplication_run(CreateApplicationAction* action);
void createapplication_free(CreateApplicationAction* action);

#endif /* SHD_CREATE_APPLICATION_H_ */
