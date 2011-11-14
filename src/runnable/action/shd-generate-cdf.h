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

#ifndef SHD_GENERATE_CDF_H_
#define SHD_GENERATE_CDF_H_

#include "shadow.h"

typedef struct _GenerateCDFAction GenerateCDFAction;

struct _GenerateCDFAction {
	Action super;
	GQuark id;
	guint64 center;
	guint64 width;
	guint64 tail;
	MAGIC_DECLARE;
};

GenerateCDFAction* generatecdf_new(GString* name, guint64 center, guint64 width,
		guint64 tail);
void generatecdf_run(GenerateCDFAction* action);
void generatecdf_free(GenerateCDFAction* action);

#endif /* SHD_GENERATE_CDF_H_ */
