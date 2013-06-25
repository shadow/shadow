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

#include <unistd.h>
#include "shadow.h"

enum EpollWatchFlags {
	EWF_NONE = 0,
	EWF_ACTIVE = 1 << 0,
	EWF_READABLE = 1 << 1,
	EWF_WAITINGREAD = 1 << 2,
	EWF_WRITEABLE = 1 << 3,
	EWF_WAITINGWRITE = 1 << 4,
};

typedef struct _EpollWatch EpollWatch;
struct _EpollWatch {
	/* the shadow descriptor we are watching for events */
	Descriptor* descriptor;
	/* the listener that will notify us when the descriptor status changes */
	Listener* listener;
	/* holds the actual event info */
	struct epoll_event event;
	/* true if this watch exists in the reportable queue */
	gboolean isReporting;
	/* true if this watch is currently valid and in the watches table. this allows
	 * support of lazy deletion of watches that are in the reportable queue when
	 * we want to delete them, to avoid the O(n) removal time of the queue.
	 */
	gboolean isWatching;
	MAGIC_DECLARE;
};

enum EpollFlags {
	EF_NONE = 0,
	/* a callback is currently scheduled to notify user (used to avoid duplicate notifications) */
	EF_SCHEDULED = 1 << 0,
};

struct _Epoll {
	/* epoll itself is also a descriptor */
	Descriptor super;

	/* other members specific to epoll */
	enum EpollFlags flags;

	/* holds the wrappers for the descriptors we are watching for events */
	GHashTable* watching;
	/* holds the watches which have events we need to report to the user */
	GQueue* reporting;

	SimulationTime lastWaitTime;
	Application* ownerApplication;
	gint osEpollDescriptor;

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

static void _epollwatch_free(EpollWatch* watch) {
	MAGIC_ASSERT(watch);

	descriptor_removeStatusListener(watch->descriptor, watch->listener);
	listener_free(watch->listener);
	descriptor_unref(watch->descriptor);

	MAGIC_CLEAR(watch);
	g_free(watch);
}

/* should only be called from descriptor dereferencing the functionTable */
static void _epoll_free(Epoll* epoll) {
	MAGIC_ASSERT(epoll);

	/* this will go through all epollwatch items and remove the listeners
	 * only from those that dont already exist in the watching table */
	while(!g_queue_is_empty(epoll->reporting)) {
		EpollWatch* watch = g_queue_pop_head(epoll->reporting);
		MAGIC_ASSERT(watch);
		watch->isReporting = FALSE;
		if(!watch->isWatching) {
			_epollwatch_free(watch);
		}
	}
	g_queue_free(epoll->reporting);

	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, epoll->watching);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		_epollwatch_free((EpollWatch*) value);
	}
	g_hash_table_destroy(epoll->watching);

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
	epoll->watching = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);
	epoll->reporting = g_queue_new();

	/* the application may want us to watch some system files, so we need a
	 * real OS epoll fd so we can offload that task.
	 */
	epoll->osEpollDescriptor = epoll_create(1000);
	if(epoll->osEpollDescriptor == -1) {
		warning("error in epoll_create for OS events, errno=%i", errno);
	}

	/* keep track of which virtual application we need to notify of events */
	Worker* worker = worker_getPrivate();
	/* epoll_new should be called as a result of an application syscall */
	g_assert(worker->cached_application);
	epoll->ownerApplication = worker->cached_application;

	return epoll;
}

static enum EpollWatchFlags _epollwatch_getStatus(EpollWatch* watch) {
	enum EpollWatchFlags flags = EWF_NONE;

	/* check status */
	enum DescriptorStatus status = descriptor_getStatus(watch->descriptor);
	flags |= (status & DS_ACTIVE) ? EWF_ACTIVE : EWF_NONE;
	flags |= (status & DS_READABLE) ? EWF_READABLE : EWF_NONE;
	flags |= (status & DS_WRITABLE) ? EWF_WRITEABLE : EWF_NONE;
	flags |= (watch->event.events & EPOLLIN) ? EWF_WAITINGREAD : EWF_NONE;
	flags |= (watch->event.events & EPOLLOUT) ? EWF_WAITINGWRITE : EWF_NONE;

	return flags;
}

static gboolean _epollwatch_needsNotify(enum EpollWatchFlags f) {
	if((f & EWF_ACTIVE) &&
			(((f & EWF_READABLE) && (f & EWF_WAITINGREAD)) ||
			((f & EWF_WRITEABLE) && (f & EWF_WAITINGWRITE)))) {
		return TRUE;
	} else {
		return FALSE;
	}
}

static void _epoll_trySchedule(Epoll* epoll) {
	/* schedule a shadow notification event if we have events to report */
	if(!g_queue_is_empty(epoll->reporting)) {
		/* avoid duplicating events in the shadow event queue for our epoll */
		gboolean isScheduled = (epoll->flags & EF_SCHEDULED) ? TRUE : FALSE;
		if(!isScheduled && application_isRunning(epoll->ownerApplication)) {
			/* schedule a notification event for our node */
			NotifyPluginEvent* event = notifyplugin_new(epoll->super.handle);
			SimulationTime delay = 1;
			worker_scheduleEvent((Event*)event, delay, 0);

			epoll->flags |= EF_SCHEDULED;
		}
	}
}

static void _epoll_check(Epoll* epoll, EpollWatch* watch) {
	MAGIC_ASSERT(epoll);
	MAGIC_ASSERT(watch);

	/* check if we need to schedule a notification */
	gboolean needsNotify = _epollwatch_needsNotify(_epollwatch_getStatus(watch));

	if(needsNotify) {
		/* we need to report an event to user */
		if(!watch->isReporting) {
			g_queue_push_tail(epoll->reporting, watch);
			watch->isReporting = TRUE;
		}
	} else {
		/* this watch no longer needs reporting
		 * NOTE: the removal is also done lazily, so this else block
		 * is technically not needed. its up for debate if its faster to
		 * iterate the queue to remove the event now, versus swapping in
		 * the node to collect events and lazily removing this later. */
		if(watch->isReporting) {
			g_queue_remove(epoll->reporting, watch);
			watch->isReporting = FALSE;
			if(!watch->isWatching) {
				_epollwatch_free(watch);
			}
		}
	}

	_epoll_trySchedule(epoll);
}

gint epoll_control(Epoll* epoll, gint operation, Descriptor* descriptor,
		struct epoll_event* event) {
	MAGIC_ASSERT(epoll);

	debug("epoll descriptor %i, operation %i, descriptor %i",
			epoll->super.handle, operation, descriptor->handle);

	EpollWatch* watch = g_hash_table_lookup(epoll->watching,
						descriptor_getHandleReference(descriptor));

	switch (operation) {
		case EPOLL_CTL_ADD: {
			/* EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor
			 * fd is already registered with this epoll instance. */
			if(watch) {
				return EEXIST;
			}

			/* start watching for status changes */
			watch = _epollwatch_new(epoll, descriptor, event);
			g_hash_table_replace(epoll->watching,
					descriptor_getHandleReference(descriptor), watch);
			watch->isWatching = TRUE;

			/* initiate a callback if the new watched descriptor is ready */
			_epoll_check(epoll, watch);

			break;
		}

		case EPOLL_CTL_MOD: {
			/* ENOENT op was EPOLL_CTL_MOD, and fd is not registered with this epoll instance */
			if(!watch) {
				return ENOENT;
			}

			MAGIC_ASSERT(watch);
			g_assert(event && watch->isWatching);

			/* the user set new events */
			watch->event = *event;

			/* initiate a callback if the new event type on the watched descriptor is ready */
			_epoll_check(epoll, watch);

			break;
		}

		case EPOLL_CTL_DEL: {
			/* ENOENT op was EPOLL_CTL_DEL, and fd is not registered with this epoll instance */
			if(!watch) {
				return ENOENT;
			}

			MAGIC_ASSERT(watch);
			g_hash_table_remove(epoll->watching,
					descriptor_getHandleReference(descriptor));
			watch->isWatching = FALSE;

			/* we can only delete it if its not in the reporting queue */
			if(!(watch->isReporting)) {
				_epollwatch_free(watch);
			} else {
				descriptor_removeStatusListener(watch->descriptor, watch->listener);
			}

			break;
		}

		default: {
			warning("ignoring unrecognized operation");
			break;
		}
	}

	return 0;
}

gint epoll_controlOS(Epoll* epoll, gint operation, gint fileDescriptor,
		struct epoll_event* event) {
	MAGIC_ASSERT(epoll);
	/* ask the OS about any events on our kernel epoll descriptor */
	return epoll_ctl(epoll->osEpollDescriptor, operation, fileDescriptor, event);
}

gint epoll_getEvents(Epoll* epoll, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents) {
	MAGIC_ASSERT(epoll);
	g_assert(nEvents);

	epoll->lastWaitTime = worker_getPrivate()->clock_now;

	/* return the available events in the eventArray, making sure not to
	 * overflow. the number of actual events is returned in nEvents. */
	gint reportableLength = g_queue_get_length(epoll->reporting);
	gint eventArrayIndex = 0;

	for(gint i = 0; i < eventArrayLength && i < reportableLength; i++) {
		EpollWatch* watch = g_queue_pop_head(epoll->reporting);
		MAGIC_ASSERT(watch);

		/* lazy-delete items that we are no longer watching */
		if(!watch->isWatching) {
			watch->isReporting = FALSE;
			_epollwatch_free(watch);
			continue;
		}

		/* double check that we should still notify this event */
		enum EpollWatchFlags status = _epollwatch_getStatus(watch);
		if(_epollwatch_needsNotify(status)) {
			/* report the event */
			eventArray[eventArrayIndex] = watch->event;
			eventArray[eventArrayIndex].events = 0;
			eventArray[eventArrayIndex].events |=
					(status & EWF_READABLE) && (status & EWF_WAITINGREAD) ? EPOLLIN : 0;
			eventArray[eventArrayIndex].events |=
					(status & EWF_WRITEABLE) && (status & EWF_WAITINGWRITE) ? EPOLLOUT : 0;
			eventArrayIndex++;
			g_assert(eventArrayIndex <= eventArrayLength);

			/* this watch persists until the descriptor status changes */
			g_queue_push_tail(epoll->reporting, watch);
			g_assert(watch->isReporting);
		} else {
			watch->isReporting = FALSE;
		}
	}

	gint space = eventArrayLength - eventArrayIndex;
	if(space) {
		/* now we have to get events from the OS descriptors */
		struct epoll_event osEvents[space];
		/* since we are in shadow context, this will be forwarded to the OS epoll */
		gint nos = epoll_wait(epoll->osEpollDescriptor, osEvents, space, 0);

		if(nos == -1) {
			warning("error in epoll_wait for OS events on epoll fd %i", epoll->osEpollDescriptor);
		}

		/* nos will fit into eventArray */
		for(gint j = 0; j < nos; j++) {
			eventArray[eventArrayIndex] = osEvents[j];
			eventArrayIndex++;
			g_assert(eventArrayIndex <= eventArrayLength);
		}
	}

	*nEvents = eventArrayIndex;

	return 0;
}

void epoll_descriptorStatusChanged(Epoll* epoll, Descriptor* descriptor) {
	MAGIC_ASSERT(epoll);

	/* make sure we are actually watching the descriptor */
	EpollWatch* watch = g_hash_table_lookup(epoll->watching,
			descriptor_getHandleReference(descriptor));

	/* if we are not watching, its an error because we shouldn't be listening */
	g_assert(watch && (watch->descriptor == descriptor));

	/* check the status and take the appropriate action */
	_epoll_check(epoll, watch);
}

void epoll_tryNotify(Epoll* epoll) {
	MAGIC_ASSERT(epoll);

	/* event is being executed from the scheduler, so its no longer scheduled */
	epoll->flags &= ~EF_SCHEDULED;

	/* we should notify the plugin only if we still have some events to report
	 * XXX: what if our watches are empty, but the OS desc has events? */
	if(!g_queue_is_empty(epoll->reporting)) {
		/* notify application to collect the reportable events */
		application_notify(epoll->ownerApplication);
	}

	/* check if we need to be notified again */
	_epoll_trySchedule(epoll);
}
