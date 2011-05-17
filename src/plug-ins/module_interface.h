/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#ifndef MODULE_INTERFACE_H_
#define MODULE_INTERFACE_H_

#include <netinet/in.h>

/**
 * This file defines the DVN module callback functions. DVN expects all modules
 * to implement this interface by defining and implementing the following
 * functions. DVN will make calls to these functions, so your module will not
 * work without them.
 */

/**
 * Called when your module is loaded.
 */
void _module_init(void);

/**
 * Called prior to unloading your module.
 */
void _module_uninit(void);

/**
 * Called to instantiate each new module instance (i.e. node).
 * argc and argv are as passed in DSIM.
 */
void _module_instantiate(int argc, char * argv[]);

/**
 * Called immediately before a module instance (i.e. node) is destroyed
 */
void _module_destroy(void);

/**
 * Called when a module instance can read data from its network through the
 * given socket.
 */
void _module_socket_readable(int socket);

/**
 * Called when a module instance can write data to its network through the
 * given socket.
 */
void _module_socket_writable(int socket);

#endif /* MODULE_INTERFACE_H_ */
