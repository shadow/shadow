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

enum EpollWatchFlags {
	EWF_NONE = 0,
	/* callback is currently scheduled to notify user */
	EWF_NOTIFY_SCHEDULED = 1 << 0,
	/* callback is currently scheduled to poll */
	EWF_POLL_SCHEDULED = 1 << 1,
	/* we should cancel the callback and self-destruct */
	EWF_CANCEL_AND_DESTROY = 1 << 2,
	/* currently executing a callback to the application */
	EWF_EXECUTING = 1 << 3,
};

typedef struct _EpollWatch EpollWatch;
struct _EpollWatch {
	Descriptor* descriptor;
	enum EpollWatchFlags flags;
	Listener* listener;
	struct epoll_event event;
	MAGIC_DECLARE;
};

struct _Epoll {
	Descriptor super;

	/* other members specific to epoll */
	GTree* watchedDescriptors;

	MAGIC_DECLARE;
};

static EpollWatch* epollwatch_new(Epoll* epoll, Descriptor* descriptor, struct epoll_event* event) {
	EpollWatch* watch = g_new0(EpollWatch, 1);
	MAGIC_INIT(watch);
	g_assert(event);

	descriptor_ref(descriptor);

	watch->descriptor = descriptor;
	watch->listener = listener_new(epoll_notify, epoll, descriptor);
	watch->flags = EWF_NONE;
	watch->event = *event;

	return watch;
}

static void epollwatch_free(gpointer data) {
	EpollWatch* watch = data;
	MAGIC_ASSERT(watch);

	descriptor_removeStateChangeListener(watch->descriptor, watch->listener);
	listener_free(watch->listener);
	descriptor_unref(watch->descriptor);

	MAGIC_CLEAR(watch);
	g_free(watch);
}

/* should only be called from descriptor dereferencing the functionTable */
static void epoll_free(gpointer data) {
	Epoll* epoll = data;
	MAGIC_ASSERT(epoll);

	/* this will go through all epollwatch items and remove the listeners */
	g_tree_destroy(epoll->watchedDescriptors);

	MAGIC_CLEAR(epoll);
	g_free(epoll);
}

DescriptorFunctionTable epollFunctions = {
	(DescriptorFreeFunc) epoll_free,
	MAGIC_VALUE
};

Epoll* epoll_new(gint handle) {
	g_assert(handle >= VNETWORK_MIN_SD);
	Epoll* epoll = g_new0(Epoll, 1);
	MAGIC_INIT(epoll);

	descriptor_init(&(epoll->super), DT_EPOLL, &epollFunctions, handle);

	/* allocate backend needed for managing events for this descriptor */
	epoll->watchedDescriptors = g_tree_new_full(descriptor_compare, NULL, NULL, epollwatch_free);

	return epoll;
}

static gboolean epoll_isWatchingDescriptor(Epoll* epoll, Descriptor* descriptor) {
	EpollWatch* watch = g_tree_lookup(epoll->watchedDescriptors,
			descriptor_getHandleReference(descriptor));
	return watch == NULL ? FALSE : TRUE;
}

gint epoll_control(Epoll* epoll, gint operation, Descriptor* descriptor,
		struct epoll_event* event) {
	MAGIC_ASSERT(epoll);

	switch (operation) {
		case EPOLL_CTL_ADD: {
			/* EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor
			 * fd is already registered with this epoll instance. */
			if(epoll_isWatchingDescriptor(epoll, descriptor)) {
				return EEXIST;
			}

			EpollWatch* watch = epollwatch_new(epoll, descriptor, event);
			g_tree_replace(epoll->watchedDescriptors, descriptor_getHandleReference(descriptor), watch);

			break;
		}

		case EPOLL_CTL_MOD: {
			/* ENOENT op was EPOLL_CTL_MOD, and fd is not
			 * registered with this epoll instance. */
			if(!epoll_isWatchingDescriptor(epoll, descriptor)) {
				return ENOENT;
			}

			EpollWatch* watch = g_tree_lookup(epoll->watchedDescriptors,
						descriptor_getHandleReference(descriptor));

			g_assert(watch && event);
			watch->event = *event;

			break;
		}

		case EPOLL_CTL_DEL: {
			/* ENOENT op was EPOLL_CTL_DEL, and fd is not
			 * registered with this epoll instance. */
			if(!epoll_isWatchingDescriptor(epoll, descriptor)) {
				return ENOENT;
			}

			g_tree_remove(epoll->watchedDescriptors, descriptor_getHandleReference(descriptor));

			break;
		}

		default: {
			warning("ignoring unrecognized operation");
			break;
		}
	}

	return 0;
}

static gboolean epoll_searchReadyEvents(gpointer key, gpointer value, gpointer data) {
	EpollWatch* watch = value;
	MAGIC_ASSERT(watch);
	GSList* readyList = data;

	// TODO
	// we return the event types that the user registered, or the event
	// types that are actually available, only if the user registered them?

	return FALSE;
}

gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents) {
	MAGIC_ASSERT(epoll);
	g_assert(nEvents);

	/* return the available events in the eventArray, making sure not to
	 * overflow. the number of actual events is returned in nEvents. */
	GSList* readyList = NULL;
	g_tree_foreach(epoll->watchedDescriptors, epoll_searchReadyEvents, readyList);

	gint i;
	GSList* next = readyList;
	for(i = 0; next && next->data && i < eventArrayLength; next = g_slist_next(next), i++) {
		struct epoll_event* event = next->data;
		g_assert(event);

		// TODO: make sure we have the correct event types set.
		// if we keep the user registered types, might need to change eventArray[nEvents].events
		eventArray[i] = *event;
	}

	if(readyList) {
		g_slist_free(readyList);
	}
	*nEvents = i;

//	The memory area pointed to by events will contain the events
//    that will be available for the caller.  Up to maxevents are returned by epoll_wait().

//    The  data  of  each  returned  structure will contain the same data the user set with
//    an epoll_ctl(2) (EPOLL_CTL_ADD,EPOLL_CTL_MOD) while the events member will contain the
//    returned event bit field.

	return -1;
}

void epoll_notify(gpointer data, gpointer callbackArgument) {
	Epoll* epoll = data;
	MAGIC_ASSERT(epoll);
	Descriptor* descriptor = callbackArgument;

	/* make sure we are actually watching the descriptor */
	EpollWatch* watch = g_tree_lookup(epoll->watchedDescriptors,
			descriptor_getHandleReference(descriptor));
	/* if we are not watching, its an error because we shouldn't be listening */
	g_assert(watch && (watch->descriptor == descriptor));

	/* check what happened to it */
	enum DescriptorStatus status = descriptor_getReady(watch->descriptor);

	/* depending on what happened and the watch state, notify application */
	// TODO
}
