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

#include "shadow.h"

struct _Pipe {
	Transport super;

	MAGIC_DECLARE;
};

TransportFunctionTable pipe_functions = {
	(TransportSendFunc) pipe_send,
	(TransportFreeFunc) pipe_free,
	MAGIC_VALUE
};

Pipe* pipe_new(gint handle) {
	Pipe* pipe = g_new0(Pipe, 1);
	MAGIC_INIT(pipe);

	transport_init(&(pipe->super), &pipe_functions, DT_PIPE, handle);

	return pipe;
}

void pipe_free(Pipe* data) {
	Pipe* pipe = data;
	MAGIC_ASSERT(pipe);

	MAGIC_CLEAR(pipe);
	g_free(pipe);
}

void pipe_send(gpointer data) {

}
