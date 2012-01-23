/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#ifndef SHD_ENGINE_H_
#define SHD_ENGINE_H_

#include <glib.h>

typedef enum _EngineStorage EngineStorage;

enum _EngineStorage {
	SOFTWARE, CDFS, PLUGINPATHS
};

typedef struct _Engine Engine;

Engine* engine_new(Configuration* config);
void engine_free(Engine* engine);
void engine_setupWorkerThreads(Engine* engine, gint nWorkerThreads);
void engine_teardownWorkerThreads(Engine* engine);

gint engine_run(Engine* engine);
void engine_pushEvent(Engine* engine, Event* event);

void engine_put(Engine* engine, EngineStorage type, GQuark* id, gpointer item);
gpointer engine_get(Engine* engine, EngineStorage type, GQuark id);

gint engine_generateWorkerID(Engine* engine);
gint engine_generateNodeID(Engine* engine);
gint engine_getNumThreads(Engine* engine);
SimulationTime engine_getMinTimeJump(Engine* engine);
SimulationTime engine_getExecutionBarrier(Engine* engine);
void engine_notifyNodeProcessed(Engine* engine);

Configuration* engine_getConfig(Engine* engine);
GTimer* engine_getRunTimer(Engine* engine);
GStaticPrivate* engine_getWorkerKey(Engine* engine);
Internetwork* engine_getInternet(Engine* engine);

void engine_setKillTime(Engine* engine, SimulationTime endTime);
gboolean engine_isKilled(Engine* engine);
gboolean engine_isForced(Engine* engine);

gint engine_nextRandomInt(Engine* engine);
gdouble engine_nextRandomDouble(Engine* engine);

#endif /* SHD_ENGINE_H_ */
