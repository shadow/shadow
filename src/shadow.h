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

#ifndef SHADOW_H_
#define SHADOW_H_

#include <glib.h>
#include <gmodule.h>

#include "engine/shd-main.h"
#include "engine/shd-configuration.h"
#include "events/shd-event.h"
#include "engine/shd-engine.h"

#include "events/shd-spinevent.h"
#include "events/shd-stopevent.h"

#include "engine/shd-logging.h"
#include "engine/shd-worker.h"

extern Engine* shadow_engine;

#endif /* SHADOW_H_ */
