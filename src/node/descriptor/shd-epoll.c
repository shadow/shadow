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

typedef struct _EpollWatch EpollWatch;
struct _EpollWatch {
	Descriptor* descriptor;
	Listener* listener;
	struct epoll_event event;
	MAGIC_DECLARE;
};

enum EpollFlags {
	EF_NONE = 0,
	/* callback is currently scheduled to notify user */
	EF_SCHEDULED = 1 << 0,
	/* we should cancel the callback and self-destruct */
	EF_CANCELED = 1 << 1,
};

struct _Epoll {
	Descriptor super;

	/* other members specific to epoll */
	enum EpollFlags flags;
	GHashTable* watchedDescriptors;

	MAGIC_DECLARE;
};

static EpollWatch* _epollwatch_new(Epoll* epoll, Descriptor* descriptor, struct epoll_event* event) {
	EpollWatch* watch = g_new0(EpollWatch, 1);
	MAGIC_INIT(watch);
	g_assert(event);

	descriptor_ref(descriptor);

	watch->listener = listener_new(epoll_descriptorStatusChanged, epoll, descriptor);
	watch->descriptor = descriptor;
	watch->event = *event;

	descriptor_addStatusChangeListener(watch->descriptor, watch->listener);

	return watch;
}

static void _epollwatch_free(gpointer data) {
	EpollWatch* watch = data;
	MAGIC_ASSERT(watch);

	descriptor_removeStatusChangeListener(watch->descriptor, watch->listener);
	listener_free(watch->listener);
	descriptor_unref(watch->descriptor);

	MAGIC_CLEAR(watch);
	g_free(watch);
}

/* should only be called from descriptor dereferencing the functionTable */
static void _epoll_free(gpointer data) {
	Epoll* epoll = data;
	MAGIC_ASSERT(epoll);

	/* this will go through all epollwatch items and remove the listeners */
	g_hash_table_destroy(epoll->watchedDescriptors);

	MAGIC_CLEAR(epoll);
	g_free(epoll);
}

DescriptorFunctionTable epollFunctions = {
	(DescriptorFreeFunc) _epoll_free,
	MAGIC_VALUE
};

Epoll* epoll_new(gint handle) {
	g_assert(handle >= MIN_DESCRIPTOR);
	Epoll* epoll = g_new0(Epoll, 1);
	MAGIC_INIT(epoll);

	descriptor_init(&(epoll->super), DT_EPOLL, &epollFunctions, handle);

	/* allocate backend needed for managing events for this descriptor */
	epoll->watchedDescriptors = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, _epollwatch_free);

	return epoll;
}

static gboolean _epoll_isWatchingDescriptor(Epoll* epoll, Descriptor* descriptor) {

	EpollWatch* watch = g_hash_table_lookup(epoll->watchedDescriptors,
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
			if(_epoll_isWatchingDescriptor(epoll, descriptor)) {
				return EEXIST;
			}

			/* start watching for status changes */
			EpollWatch* watch = _epollwatch_new(epoll, descriptor, event);
			g_hash_table_replace(epoll->watchedDescriptors, descriptor_getHandleReference(descriptor), watch);

			/* make sure to initiate a callback if we are ready right now */
			epoll_descriptorStatusChanged(epoll, descriptor);

			break;
		}

		case EPOLL_CTL_MOD: {
			/* ENOENT op was EPOLL_CTL_MOD, and fd is not
			 * registered with this epoll instance. */
			if(!_epoll_isWatchingDescriptor(epoll, descriptor)) {
				return ENOENT;
			}

			EpollWatch* watch = g_hash_table_lookup(epoll->watchedDescriptors,
						descriptor_getHandleReference(descriptor));

			g_assert(watch && event);
			watch->event = *event;

			break;
		}

		case EPOLL_CTL_DEL: {
			/* ENOENT op was EPOLL_CTL_DEL, and fd is not
			 * registered with this epoll instance. */
			if(!_epoll_isWatchingDescriptor(epoll, descriptor)) {
				return ENOENT;
			}

			g_hash_table_remove(epoll->watchedDescriptors, descriptor_getHandleReference(descriptor));

			break;
		}

		default: {
			warning("ignoring unrecognized operation");
			break;
		}
	}

	return 0;
}

gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents) {
	MAGIC_ASSERT(epoll);
	g_assert(nEvents);

	/* return the available events in the eventArray, making sure not to
	 * overflow. the number of actual events is returned in nEvents.
	 *
	 * @todo: could be more efficient if we kept track of which descriptors
	 * are ready at any given time, at the cost of code complexity (we'd have
	 * to manage descriptors in multiples structs, update when deleted, etc)
	 */
	GHashTableIter iter;
	gpointer key, value;
	gint i = 0;
	g_hash_table_iter_init(&iter, epoll->watchedDescriptors);

	while(g_hash_table_iter_next(&iter, &key, &value)) {
		EpollWatch* watch = value;
		MAGIC_ASSERT(watch);

		/* return the event types that are actually available if they are registered
		 * without removing our knowledge of what was registered
		 */
		enum DescriptorStatus status = descriptor_getStatus(watch->descriptor);
		gboolean isReadable = (status & DS_READABLE) ? TRUE : FALSE;
		gboolean waitingReadable = (watch->event.events & EPOLLIN) ? TRUE : FALSE;
		gboolean isWritable = (status & DS_WRITABLE) ? TRUE : FALSE;
		gboolean waitingWritable = (watch->event.events & EPOLLOUT) ? TRUE : FALSE;

		if((isReadable && waitingReadable) || (isWritable && waitingWritable)) {
			eventArray[i] = watch->event;
			eventArray[i].events =
					isReadable && isWritable ? EPOLLIN|EPOLLOUT : isReadable ? EPOLLIN : EPOLLOUT;
			i++;
		}

		/* if we've filled everything, stop iterating */
		if(i >= eventArrayLength) {
			break;
		}
	}

	*nEvents = i;

	return 0;
}

static gboolean _epollwatch_needsNotify(EpollWatch* watch) {
	MAGIC_ASSERT(watch);

	/* check status */
	enum DescriptorStatus status = descriptor_getStatus(watch->descriptor);

	/* check if we care about the status */
	gboolean isActive = (status & DS_ACTIVE) ? TRUE : FALSE;
	gboolean isReadable = (status & DS_READABLE) ? TRUE : FALSE;
	gboolean waitingReadable = (watch->event.events & EPOLLIN) ? TRUE : FALSE;
	gboolean isWritable = (status & DS_WRITABLE) ? TRUE : FALSE;
	gboolean waitingWritable = (watch->event.events & EPOLLOUT) ? TRUE : FALSE;

	if(isActive && ((isReadable && waitingReadable) || (isWritable && waitingWritable))) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void epoll_descriptorStatusChanged(gpointer data, gpointer callbackArgument) {
	Epoll* epoll = data;
	MAGIC_ASSERT(epoll);
	Descriptor* descriptor = callbackArgument;

	/* make sure we are actually watching the descriptor */
	EpollWatch* watch = g_hash_table_lookup(epoll->watchedDescriptors,
			descriptor_getHandleReference(descriptor));
	/* if we are not watching, its an error because we shouldn't be listening */
	g_assert(watch && (watch->descriptor == descriptor));

	/* check if we need to schedule a notification */
	gboolean needsNotify = _epollwatch_needsNotify(watch);
	gboolean isScheduled = (epoll->flags & EF_SCHEDULED) ? TRUE : FALSE;
	if(needsNotify && !isScheduled) {
		epoll->flags |= EF_SCHEDULED;
		NotifyPluginEvent* event = notifyplugin_new(epoll->super.handle);
		SimulationTime delay = 1;
		/* notify ourselves */
		worker_scheduleEvent((Event*)event, delay, 0);
	} else if(!needsNotify && isScheduled) {
		epoll->flags |= EF_CANCELED;
	}
}

gboolean epoll_isReadyToNotify(Epoll* epoll) {
	MAGIC_ASSERT(epoll);

	/* event is being executed from the scheduler */
	epoll->flags &= ~EF_SCHEDULED;

	/* double check that we should actually notify the plugin */
	if(epoll->flags & EF_CANCELED) {
		epoll->flags &= ~EF_CANCELED;
		return FALSE;
	} else {
		return TRUE;
	}
}
