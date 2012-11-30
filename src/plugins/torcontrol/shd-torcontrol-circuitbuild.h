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

#ifndef SHD_TOR_CTL_CIRCUITBUILD_H_
#define SHD_TOR_CTL_CIRCUITBUILD_H_

#include <glib.h>
#include <shd-library.h>

#include "shd-torcontrol.h"

typedef struct _TorCtlCircuitBuild TorCtlCircuitBuild;

TorCtlCircuitBuild *torControlCircuitBuild_new(ShadowLogFunc logFunc, gint sockd, gchar **args, TorControl_EventHandlers *handlers);

#endif /* SHD_TOR_CTL_CIRCUITBUILD_H_ */
