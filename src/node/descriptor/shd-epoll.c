/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#include <unistd.h>
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
};

struct _Epoll {
	Descriptor super;

	/* other members specific to epoll */
	enum EpollFlags flags;

	/* wrappers for descriptors we are watching for events */
	GHashTable* watches;
	/* wrappers for descriptors with events that should be reported to user */
	GHashTable* reports;

	gint osEpollDescriptor;
	SimulationTime lastWaitTime;

	MAGIC_DECLARE;
};

static EpollWatch* _epollwatch_new(Epoll* epoll, Descriptor* descriptor, struct epoll_event* event) {
	EpollWatch* watch = g_new0(EpollWatch, 1);
	MAGIC_INIT(watch);
	g_assert(event);

	/* ref it for the EpollWatch, which also covers the listener reference
	 * (which is freed below in _epollwatch_free) */
	descriptor_ref(descriptor);

	watch->listener = listener_new((CallbackFunc)epoll_descriptorStatusChanged, epoll, descriptor);
	watch->descriptor = descriptor;
	watch->event = *event;

	descriptor_addStatusListener(watch->descriptor, watch->listener);

	return watch;
}

static void _epollwatch_free(gpointer data) {
	EpollWatch* watch = data;
	MAGIC_ASSERT(watch);

	descriptor_removeStatusListener(watch->descriptor, watch->listener);
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
	g_hash_table_destroy(epoll->watches);
	g_hash_table_destroy(epoll->reports);

	close(epoll->osEpollDescriptor);

	MAGIC_CLEAR(epoll);
	g_free(epoll);
}

static void _epoll_close(Epoll* epoll) {
	MAGIC_ASSERT(epoll);
	descriptor_adjustStatus(&(epoll->super), DS_CLOSED, TRUE);
	node_closeDescriptor(worker_getPrivate()->cached_node, epoll->super.handle);
}

DescriptorFunctionTable epollFunctions = {
	(DescriptorFunc) _epoll_close,
	(DescriptorFunc) _epoll_free,
	MAGIC_VALUE
};

Epoll* epoll_new(gint handle) {
	g_assert(handle >= MIN_DESCRIPTOR);
	Epoll* epoll = g_new0(Epoll, 1);
	MAGIC_INIT(epoll);

	descriptor_init(&(epoll->super), DT_EPOLL, &epollFunctions, handle);

	/* allocate backend needed for managing events for this descriptor */
	epoll->watches = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, _epollwatch_free);
	epoll->reports = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);

	/* the application may want us to watch some system files, so we need a
	 * real OS epoll fd so we can offload that task.
	 */
	epoll->osEpollDescriptor = epoll_create(1000);
	if(epoll->osEpollDescriptor == -1) {
		warning("error in epoll_create for OS events, errno=%i", errno);
	}

	return epoll;
}

static gboolean _epoll_isWatchingDescriptor(Epoll* epoll, Descriptor* descriptor) {

	EpollWatch* watch = g_hash_table_lookup(epoll->watches,
			descriptor_getHandleReference(descriptor));
	return watch == NULL ? FALSE : TRUE;
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

static void _epoll_check(Epoll* epoll, EpollWatch* watch) {
	MAGIC_ASSERT(epoll);
	MAGIC_ASSERT(watch);

	/* check if we need to schedule a notification */
	gboolean needsNotify = _epollwatch_needsNotify(watch);
	gboolean isScheduled = (epoll->flags & EF_SCHEDULED) ? TRUE : FALSE;

	gpointer watchKey = descriptor_getHandleReference(watch->descriptor);

	if(needsNotify) {
		/* we need to report an event to user */
		g_hash_table_replace(epoll->reports, watchKey, watch);

		if(!isScheduled) {
			/* schedule a notification event for our node */
			NotifyPluginEvent* event = notifyplugin_new(epoll->super.handle);
			SimulationTime delay = 1;
			worker_scheduleEvent((Event*)event, delay, 0);

			epoll->flags |= EF_SCHEDULED;
		}
	} else {
		/* no longer needs reporting */
		g_hash_table_remove(epoll->reports, watchKey);
	}
}

gint epoll_control(Epoll* epoll, gint operation, Descriptor* descriptor,
		struct epoll_event* event) {
	MAGIC_ASSERT(epoll);

	debug("epoll descriptor %i, operation %i, descriptor %i",
			epoll->super.handle, operation, descriptor->handle);

	switch (operation) {
		case EPOLL_CTL_ADD: {
			/* EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor
			 * fd is already registered with this epoll instance. */
			if(_epoll_isWatchingDescriptor(epoll, descriptor)) {
				return EEXIST;
			}

			/* start watching for status changes */
			EpollWatch* watch = _epollwatch_new(epoll, descriptor, event);
			g_hash_table_replace(epoll->watches, descriptor_getHandleReference(descriptor), watch);

			/* initiate a callback if the new watched descriptor is ready */
			_epoll_check(epoll, watch);

			break;
		}

		case EPOLL_CTL_MOD: {
			/* ENOENT op was EPOLL_CTL_MOD, and fd is not
			 * registered with this epoll instance. */
			if(!_epoll_isWatchingDescriptor(epoll, descriptor)) {
				return ENOENT;
			}

			EpollWatch* watch = g_hash_table_lookup(epoll->watches,
						descriptor_getHandleReference(descriptor));

			g_assert(watch && event);
			watch->event = *event;

			/* initiate a callback if the new event type on the watched descriptor is ready */
			_epoll_check(epoll, watch);

			break;
		}

		case EPOLL_CTL_DEL: {
			/* ENOENT op was EPOLL_CTL_DEL, and fd is not
			 * registered with this epoll instance. */
			if(!_epoll_isWatchingDescriptor(epoll, descriptor)) {
				return ENOENT;
			}

			g_hash_table_remove(epoll->watches, descriptor_getHandleReference(descriptor));

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

	epoll->lastWaitTime = worker_getPrivate()->clock_now;

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
	g_hash_table_iter_init(&iter, epoll->watches);

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
			/* report the event */
			eventArray[i] = watch->event;
			eventArray[i].events =
					isReadable && isWritable ? EPOLLIN|EPOLLOUT : isReadable ? EPOLLIN : EPOLLOUT;

			/* no longer needs to be reported. if the user does not take action,
			 * we'll mark it as needs reporting again in ensureNotifyTriggers */
			g_hash_table_remove(epoll->reports, descriptor_getHandleReference(watch->descriptor));

			i++;
		}

		/* if we've filled everything, stop iterating */
		if(i >= eventArrayLength) {
			break;
		}
	}

	if(i < eventArrayLength) {
		/* now we have to get events from the OS descriptors */
		struct epoll_event osEvents[20];
		/* since we are in shadow context, this will be forwarded to the OS epoll */
		gint nos = epoll_wait(epoll->osEpollDescriptor, osEvents, 20, 0);

		if(nos == -1) {
			warning("error in epoll_wait for OS events on epoll fd %i", epoll->osEpollDescriptor);
		}

		for(gint j = 0; j < nos; j++) {
			eventArray[i] = osEvents[j];
			i++;
			/* if we've filled everything, stop iterating */
			if(i >= eventArrayLength) {
				break;
			}
		}
	}

	*nEvents = i;

	return 0;
}

void epoll_descriptorStatusChanged(Epoll* epoll, Descriptor* descriptor) {
	MAGIC_ASSERT(epoll);

	/* make sure we are actually watching the descriptor */
	EpollWatch* watch = g_hash_table_lookup(epoll->watches,
			descriptor_getHandleReference(descriptor));

	/* if we are not watching, its an error because we shouldn't be listening */
	g_assert(watch && (watch->descriptor == descriptor));

	/* trigger notify if needed */
	_epoll_check(epoll, watch);
}

void epoll_ensureTriggers(Epoll* epoll) {
	MAGIC_ASSERT(epoll);

	/* check all of our watched descriptors and schedule a notification if there
	 * are any with events that need to be reported */
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, epoll->watches);

	while(g_hash_table_iter_next(&iter, &key, &value)) {
		EpollWatch* watch = value;
		_epoll_check(epoll, watch);
	}
}

gboolean epoll_isReadyToNotify(Epoll* epoll) {
	MAGIC_ASSERT(epoll);

	/* event is being executed from the scheduler */
	epoll->flags &= ~EF_SCHEDULED;

	/* we should notify the plugin only if we still have some events to report */
	if(g_hash_table_size(epoll->reports) > 0) {
		return TRUE;
	} else {
		return FALSE;
	}
}

gint epoll_getOSEpollDescriptor(Epoll* epoll) {
	MAGIC_ASSERT(epoll);
	return epoll->osEpollDescriptor;
}
