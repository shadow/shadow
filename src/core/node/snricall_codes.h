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

#ifndef _snricall_codes_h
#define _snricall_codes_h

#define SNRICALL_ERROR -1
#define SNRICALL_SUCCESS 0

/**
 * int*		    - output/number of IPs assigned
 * in_addr_t**- output/array of assigned IPs
 */
#define SNRICALL_GETIP 1

/**
 * logs a string
 * int			- log level
 * unsigned int - length of data
 * char *		- data to log
 */
#define SNRICALL_LOG 2

/**
 * logs a binary message
 * int			- log level
 * unsigned int - length of data
 * char *		- data to log
 */
#define SNRICALL_LOG_BINARY 3

/**
 * kills off the current node
 * no arguments
 */
#define SNRICALL_EXIT 4

/**
 * creates a timer
 * unsigned int	- number of milliseconds from now the timer should expire
 * void (*t)(int timerid) - timer callback - function called when timer expires
 *
 * int* 		- output/timer identifier
 */
#define SNRICALL_CREATE_TIMER 5

/**
 * destroys a timer
 * int			- timer id to destroy
 */
#define SNRICALL_DESTROY_TIMER 6

/**
 * returns the system time
 * struct timeval* - output/system time
 */
#define SNRICALL_GETTIME 7

#define SNRICALL_REGISTER_GLOBALS 8

/**
 * char*  		- hostname to resolve to an addr
 * in_addr_t* 	- pointer to space for the returned addr
 */
#define SNRICALL_RESOLVE_NAME 9

/**
 * in_addr_t	- the addr to resolve to a name
 * char* 		- pointer to a buffer to hold the resolved name
 * int			- the length of the buffer
 */
#define SNRICALL_RESOLVE_ADDR 10

/**
 * in_addr_t	- the addr to find the minimum of up/down bw
 * uint*		- will hold the result
 */
#define SNRICALL_RESOLVE_BW 11

/**
 * int			-the socket descriptor
 * int*			-pointer to an int to hold the result, 1 or 0 (true/false)
 */
#define SNRICALL_SOCKET_IS_READABLE 12

/**
 * int			-the socket descriptor
 * int*			-pointer to an int to hold the result, 1 or 0 (true/false)
 */
#define SNRICALL_SOCKET_IS_WRITABLE 13

#define SNRICALL_SET_LOOPEXIT_FN 16

#endif
