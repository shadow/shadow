/*
 * vevent_test.c
 *
 *  Created on: Apr 29, 2010
 *      Author: rob
 *
 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include <event2/event_struct.h>
#include <event2/event.h>
#include <event2/util.h>

/* This module implements the module_interface */
#include "module_interface.h"

/* This module makes calls to the DVN core through the standard network routing interface */
#include "snri.h"

#include "vevent.h"

/* define the following if we want to test the preload library */
//#define PRELOADTEST 1
//#ifdef PRELOADTEST
//#define CALL(x) x
//#else
//#include "vevent_intercept.h"
//#define CALL(x) vevent_intercept_##x
//#endif

#define VEVENT_TEST_ISVALID (void*)0xAFAEADAC

typedef struct vevent_test_s {
	vevent_mgr_t vevent;
	event_base_tp global_base;
	event_tp normal;
	event_tp timer;
	event_tp persistent;
	event_tp persistent_timer;
	event_tp success;
	int is_success;
	int num_activated;
	void* memalign;
} vevent_test_t, *vevent_test_tp;

vevent_test_t _global_data;
vevent_test_tp globals = NULL;

void vevent_test_event(evutil_socket_t sd, short types, void* arg) {
	/* this means all other events passed */
	snri_log(LOG_MSG, "vevent_test_event_cb: sd %i\n", sd);
	vevent_test_tp vet = arg;
	assert(vet != NULL);
	assert(vet->memalign == VEVENT_TEST_ISVALID);
	vet = NULL;
	globals->num_activated++;
}

void vevent_test_success_cb(evutil_socket_t sd, short types, void* arg) {
	vevent_test_event(sd, types, arg);
	globals->is_success = 1;
	event_del(globals->success);
	event_free(globals->success);
	globals->success = NULL;
	snri_log(LOG_MSG, "vevent_test_success_cb: success, sd %i\n", sd);
}

void vevent_test_normal_cb(evutil_socket_t sd, short types, void* arg) {
	vevent_test_event(sd, types, arg);
	assert(globals->normal != NULL);
	assert(!event_pending(globals->normal, (EV_READ|EV_WRITE), NULL));
	assert(event_del(globals->normal) == 0);
	event_free(globals->normal);
	globals->normal = NULL;
	snri_log(LOG_MSG, "vevent_test_normal_cb: success, sd %i\n", sd);
}

void vevent_test_timer_cb(evutil_socket_t sd, short types, void* arg) {
	vevent_test_event(sd, types, arg);
	assert(globals->timer != NULL);
	assert(!event_pending(globals->timer, EV_TIMEOUT, NULL));
	assert(event_del(globals->timer) == 0);
	event_free(globals->timer);
	globals->timer = NULL;
	snri_log(LOG_MSG, "vevent_test_timer_cb: success, sd %i\n", sd);
}

void vevent_test_persistent_cb(evutil_socket_t sd, short types, void* arg) {
	vevent_test_event(sd, types, arg);
	assert(globals->persistent != NULL);
	assert(event_pending(globals->persistent, (EV_READ|EV_WRITE), NULL));
	assert(event_del(globals->persistent) == 0);
	event_free(globals->persistent);
	globals->persistent = NULL;
	snri_log(LOG_MSG, "vevent_test_persistent_cb: success, sd %i\n", sd);
}

void vevent_test_persistent_timer_cb(evutil_socket_t sd, short types, void* arg) {
	vevent_test_event(sd, types, arg);
	assert(globals->persistent_timer != NULL);
	assert(event_pending(globals->persistent_timer, EV_TIMEOUT, NULL));
	assert(event_del(globals->persistent_timer) == 0);
	event_free(globals->persistent_timer);
	globals->persistent_timer = NULL;
	snri_log(LOG_MSG, "vevent_test_persistent_timer_cb: success, sd %i\n", sd);
}

void vevent_test_event_base_new() {
	event_base_tp eb = event_base_new();
	assert(eb != NULL);
	event_base_free(eb);
}

void vevent_test_event_base_free() {
	vevent_test_event_base_new();
}

void vevent_test_event_base_get_method() {
	assert(event_base_get_method(globals->global_base) != NULL);
}

void vevent_test_event_set_log_callback() {
	event_set_log_callback(NULL);
}

void vevent_test_event_base_loop() {
	event_base_loop(globals->global_base, 0);
}

void vevent_test_event_base_loopexit() {
	event_base_loopexit(globals->global_base, NULL);
}

void vevent_test_event_new() {
	int key = 1;
	globals->normal = event_new(globals->global_base, key, (EV_READ|EV_WRITE), &vevent_test_normal_cb, globals);
	assert(globals->normal != NULL);

	assert(globals->normal->ev_fd == key);
	assert(globals->normal->ev_events == (EV_READ|EV_WRITE));
	assert(globals->normal->ev_arg == globals);
	assert(globals->normal->ev_callback == &vevent_test_normal_cb);
	assert(globals->normal->ev_base == globals->global_base);

	key++;

	globals->persistent = event_new(globals->global_base, key, (EV_READ|EV_WRITE|EV_PERSIST), &vevent_test_persistent_cb, globals);
	assert(globals->persistent != NULL);

	assert(globals->persistent->ev_fd == key);
	assert(globals->persistent->ev_events == (EV_READ|EV_WRITE|EV_PERSIST));
	assert(globals->persistent->ev_arg == globals);
	assert(globals->persistent->ev_callback == &vevent_test_persistent_cb);
	assert(globals->persistent->ev_base == globals->global_base);

	globals->timer = event_new(globals->global_base, -1, EV_TIMEOUT, &vevent_test_timer_cb, globals);
	assert(globals->timer != NULL);

	assert(globals->timer->ev_fd == -1);
	assert(globals->timer->ev_events == EV_TIMEOUT);
	assert(globals->timer->ev_arg == globals);
	assert(globals->timer->ev_callback == &vevent_test_timer_cb);
	assert(globals->timer->ev_base == globals->global_base);

	globals->persistent_timer = event_new(globals->global_base, -1, (EV_TIMEOUT|EV_PERSIST), &vevent_test_persistent_timer_cb, globals);
	assert(globals->persistent_timer != NULL);

	assert(globals->persistent_timer->ev_fd == -1);
	assert(globals->persistent_timer->ev_events == (EV_TIMEOUT|EV_PERSIST));
	assert(globals->persistent_timer->ev_arg == globals);
	assert(globals->persistent_timer->ev_callback == &vevent_test_persistent_timer_cb);
	assert(globals->persistent_timer->ev_base == globals->global_base);
}

void vevent_test_event_free() {
	event_free(globals->normal);
	globals->normal = NULL;
	event_free(globals->persistent);
	globals->persistent = NULL;
	event_free(globals->timer);
	globals->timer = NULL;
	event_free(globals->persistent_timer);
	globals->persistent_timer = NULL;
}

void vevent_test_event_add() {
	assert(event_pending(globals->normal, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);
	assert(event_add(globals->normal, NULL) == 0);
	assert(event_pending(globals->normal, (EV_READ|EV_WRITE), NULL) == 1);
	assert(event_pending(globals->normal, (EV_SIGNAL|EV_TIMEOUT), NULL) == 0);

	assert(event_pending(globals->persistent, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);
	assert(event_add(globals->persistent, NULL) == 0);
	assert(event_pending(globals->persistent, (EV_READ|EV_WRITE), NULL) == 1);
	assert(event_pending(globals->persistent, (EV_SIGNAL|EV_TIMEOUT), NULL) == 0);

	/* TODO what happens if you add a timer event with a null timeout?
	 * probably the same thing as adding any other event with a null timeout...*/
	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	assert(event_pending(globals->timer, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);
	assert(event_add(globals->timer, &timeout) == 0);
	assert(event_pending(globals->timer, EV_TIMEOUT, NULL) == 1);
	assert(event_pending(globals->timer, (EV_WRITE|EV_READ|EV_SIGNAL), NULL) == 0);

	assert(event_pending(globals->persistent_timer, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);
	assert(event_add(globals->persistent_timer, &timeout) == 0);
	assert(event_pending(globals->persistent_timer, EV_TIMEOUT, NULL) == 1);
	assert(event_pending(globals->persistent_timer, (EV_WRITE|EV_READ|EV_SIGNAL), NULL) == 0);
}

void vevent_test_event_del() {
	assert(event_del(globals->normal) == 0);
	assert(event_pending(globals->normal, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);

	assert(event_del(globals->persistent) == 0);
	assert(event_pending(globals->persistent, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);

	assert(event_del(globals->timer) == 0);
	assert(event_pending(globals->timer, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);

	assert(event_del(globals->persistent_timer) == 0);
	assert(event_pending(globals->persistent_timer, (EV_WRITE|EV_READ|EV_TIMEOUT|EV_SIGNAL), NULL) == 0);
}

void vevent_test_event_active() {
	/* TODO whats the bevavior when activating events? do they lose persistence? are timeouts canceled? */
	event_active(globals->normal, globals->normal->ev_events, 1);
	event_active(globals->persistent, globals->persistent->ev_events, 1);
}

void vevent_test_event_pending() {
	event_t ev;
	memset(&ev, 0, sizeof(event_t));

	assert(event_assign(&ev, globals->global_base, -1, 0, NULL, NULL) == 0);
	assert(event_add(&ev, NULL) == 0);
	assert(event_pending(&ev, EV_TIMEOUT, NULL));
	assert(event_del(&ev) == 0);

	assert(event_assign(&ev, globals->global_base, 1, EV_TIMEOUT, NULL, NULL) == 0);
	assert(event_add(&ev, NULL) == 0);
	assert(event_pending(&ev, EV_TIMEOUT, NULL));
	assert(event_del(&ev) == 0);

	assert(event_assign(&ev, globals->global_base, 1, (EV_READ|EV_WRITE|EV_SIGNAL|EV_PERSIST), NULL, NULL) == 0);
	assert(event_add(&ev, NULL) == 0);
	assert(event_pending(&ev, EV_READ, NULL));
	assert(event_pending(&ev, EV_WRITE, NULL));
	assert(event_pending(&ev, EV_SIGNAL, NULL));
	assert(event_pending(&ev, EV_TIMEOUT, NULL) == 0);
	assert(event_del(&ev) == 0);
}

void vevent_test_event_get_version() {
	assert(event_get_version() != NULL);
}

void vevent_test_event_get_version_number() {
	assert(event_get_version_number() != -1);
}

void _module_init() {
	snri_log(LOG_MSG, "_module_init: initializing vevent_test module\n");
	globals = &_global_data;
	snri_register_globals(2, sizeof(vevent_test_t), globals, sizeof(vevent_test_tp), &globals);
}

void _module_uninit() {
	snri_log(LOG_MSG, "_module_uninit: un-initializing vevent_test module\n");
}

void _module_instantiate(int argc, char * argv[], in_addr_t bootstrap_address) {
	snri_log(LOG_MSG, "_module_instantiate: instantiating vevent_test node\n");

	vevent_mgr_init(&globals->vevent, &snri_timer_create, NULL);

	globals->memalign = VEVENT_TEST_ISVALID;
	globals->global_base = event_base_new();
	globals->is_success = 0;
	globals->num_activated = 0;

	assert(globals->global_base != NULL);

	/* run the non event tests */
	vevent_test_event_base_new();
	vevent_test_event_base_free();
	vevent_test_event_base_get_method();
	vevent_test_event_base_loop();
	vevent_test_event_base_loopexit();
	vevent_test_event_pending();

	/* run the event tests */
	vevent_test_event_new();
	vevent_test_event_free();

	vevent_test_event_new();
	vevent_test_event_add();
	vevent_test_event_del();
	vevent_test_event_add();
	vevent_test_event_active();

	globals->success = evtimer_new(globals->global_base, &vevent_test_success_cb, globals);
	struct timeval tv;
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	evtimer_add(globals->success, &tv);
}

void _module_destroy() {
	char* msg;
	if(globals->is_success) {
		msg = "all tests SUCCESSFUL!";
	} else {
		msg = "FAILED some tests!";
	}

	snri_log(LOG_MSG, "_module_destroy: %s\n", msg);

	snri_log(LOG_INFO, "_module_destroy: destroying vevent_test node\n");
	event_base_free(globals->global_base);

	vevent_mgr_uninit();
}

void _module_socket_readable(int sockd) {}
void _module_socket_writable(int sockd) {}
