/*
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

#ifndef SHD_CALLBACK_H_
#define SHD_CALLBACK_H_

#include "shadow.h"

typedef void (*CallbackFunc)(gpointer data, gpointer callbackArgument);

typedef struct _CallbackEvent CallbackEvent;

struct _CallbackEvent {
	Event super;
	CallbackFunc callback;
	gpointer data;
	gpointer callbackArgument;

	MAGIC_DECLARE;
};

CallbackEvent* callback_new(CallbackFunc callback, gpointer data, gpointer callbackArgument);
void callback_run(CallbackEvent* event, Node* node);
void callback_free(CallbackEvent* event);

#endif /* SHD_CALLBACK_H_ */
