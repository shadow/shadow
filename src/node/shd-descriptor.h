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

#ifndef SHD_DESCRIPTOR_H_
#define SHD_DESCRIPTOR_H_

#include "shadow.h"

enum DescriptorType {
	DT_SOCKET, DT_EPOLL
};

enum DescriptorFlags {
	DF_NONE = 0,
	DF_A = 1 << 0,
	DF_B = 1 << 1,
	DF_C = 1 << 2,
};

typedef struct _Descriptor Descriptor;
typedef struct _DescriptorFunctionTable DescriptorFunctionTable;

/* required functions */
typedef void (*DescriptorFreeFunc)(Descriptor* descriptor);

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _DescriptorFunctionTable {
	DescriptorFreeFunc free;
	MAGIC_DECLARE;
};

struct _Descriptor {
	DescriptorFunctionTable* funcTable;
	gint handle;
	enum DescriptorType type;
	MAGIC_DECLARE;
};

void descriptor_init(Descriptor* descriptor, enum DescriptorType type,
		DescriptorFunctionTable* funcTable, gint handle);
void descriptor_free(gpointer data);
gint descriptor_compare(gconstpointer a, gconstpointer b, gpointer user_data);

enum DescriptorType descriptor_getType(Descriptor* descriptor);
gint* descriptor_getHandleReference(Descriptor* descriptor);

#endif /* SHD_DESCRIPTOR_H_ */
