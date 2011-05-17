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

#ifndef _sysconfig_h
#define _sysconfig_h

#include "hashtable.h"

#define SYSCONFIG_INT 1
#define SYSCONFIG_STRING  2
#define SYSCONFIG_FLOAT 3

#define SYSCONFIG_LOCK_STR_CUSTOM "custom"
#define SYSCONFIG_LOCK_STR_PTHREAD "pthread"
#define SYSCONFIG_LOCK_STR_SEMAPHORE "semaphore"
#define SYSCONFIG_LOGLEVEL_STRING "message"

typedef struct sysconfig_val_t {
	char name[128];
	int type;
	union {
		char string_val[128];
		float float_val;
		int int_val;
	} v;
} sysconfig_val_t, * sysconfig_val_tp;

typedef struct sysconfig_t {
	hashtable_tp data;
	unsigned int exported_config_size;
	char exported_config[65536]; /* hope that's big enough! */
} sysconfig_t, * sysconfig_tp;

extern sysconfig_t sysconfig;

void sysconfig_init(void) ;
void sysconfig_destroy_cb(void* value, int key);

int sysconfig_get_int(char * param);
float sysconfig_get_float(char * param);
char * sysconfig_get_string(char * param);

void sysconfig_set_int(char * param, int v);
void sysconfig_set_string(char * param, char * v);
void sysconfig_set_float(char * param, float v);

void sysconfig_import_config(char * config_data);
char * sysconfig_export_config(void);

void sysconfig_cleanup(void) ;

#endif
