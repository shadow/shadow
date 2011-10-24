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

#ifndef SCALLION_PLUGIN_H_
#define SCALLION_PLUGIN_H_

#include <glib.h>
#include <shd-library.h>
#include <shd-filetransfer.h>

#include "vtor.h"
#include "vtorflow.h"

typedef struct _Scallion Scallion;
struct _Scallion {
	in_addr_t ip;
	char ipstring[40];
	char hostname[128];
	vtor_t vtor;
	service_filegetter_t sfg;
	ShadowlibFunctionTable* shadowlibFuncs;
};

/* allow access to globals of the current scallion context */
//extern Scallion scallion;

/* register scallion and tor globals */
void scallion_register_globals(PluginFunctionTable* scallionFuncs, Scallion* scallionData);

void scallion_new(int argc, char* argv[]);
void scallion_free();
void scallion_readable(int socketDesriptor);
void scallion_writable(int socketDesriptor);


#endif /* SCALLION_PLUGIN_H_ */
