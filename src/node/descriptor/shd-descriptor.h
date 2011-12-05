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
	DT_TCPSOCKET, DT_UDPSOCKET, DT_PIPE, DT_SOCKETPAIR, DT_EPOLL
};

enum DescriptorStatus {
	DS_NONE = 0,
	/* ok to notify user as far as we know, socket is ready.
	 * o/w never notify user (b/c they e.g. closed the socket or did not accept yet) */
	DS_ACTIVE = 1 << 0,
	/* can be read, i.e. there is data waiting for user */
	DS_READABLE = 1 << 1,
	/* can be written, i.e. there is available buffer space */
	DS_WRITABLE = 1 << 2,
	/* user already called close */
	DS_CLOSED = 1 << 3,
};

typedef struct _Descriptor Descriptor;
typedef struct _DescriptorFunctionTable DescriptorFunctionTable;

/* required functions */
typedef void (*DescriptorFunc)(Descriptor* descriptor);

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _DescriptorFunctionTable {
	DescriptorFunc close;
	DescriptorFunc free;
	MAGIC_DECLARE;
};

struct _Descriptor {
	DescriptorFunctionTable* funcTable;
	gint handle;
	enum DescriptorType type;
	enum DescriptorStatus status;
	GSList* readyListeners;
	gint referenceCount;
	MAGIC_DECLARE;
};

void descriptor_init(Descriptor* descriptor, enum DescriptorType type,
		DescriptorFunctionTable* funcTable, gint handle);
void descriptor_ref(gpointer data);
void descriptor_unref(gpointer data);
void descriptor_close(Descriptor* descriptor);
gint descriptor_compare(const Descriptor* foo, const Descriptor* bar, gpointer user_data);

enum DescriptorType descriptor_getType(Descriptor* descriptor);
gint* descriptor_getHandleReference(Descriptor* descriptor);

void descriptor_adjustStatus(Descriptor* descriptor, enum DescriptorStatus status, gboolean doSetBits);
enum DescriptorStatus descriptor_getStatus(Descriptor* descriptor);

void descriptor_addStatusListener(Descriptor* descriptor, Listener* listener);
void descriptor_removeStatusListener(Descriptor* descriptor, Listener* listener);

#endif /* SHD_DESCRIPTOR_H_ */
