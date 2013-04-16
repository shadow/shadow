/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
