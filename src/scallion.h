/*
 * scallion.h
 *
 *  Created on: Mar 4, 2011
 *      Author: rob
 */

#ifndef SCALLION_H_
#define SCALLION_H_

#include <netinet/in.h>

#include <shd-plugin.h>
#include <shd-service-filegetter.h>
#include "vtor.h"

typedef struct scallion_s {
	in_addr_t ip;
	char ipstring[40];
	char hostname[128];
	vtor_t vtor;
	service_filegetter_t sfg;
} scallion_t, *scallion_tp;

/* allow access to globals of the current scallion context */
extern scallion_tp scallion;

/* register scallion and tor globals */
void scallion_register_globals(scallion_t* scallion_global_data, scallion_tp* scallion);

/* liason for snri timers, so we can properly enter the scallion context.
 * will call the given callback in scallion context when the timer expires.
 * returns timerid */
int scallion_create_timer(int milli_delay, snri_timer_callback_fp cb_function, void * cb_arg);

/* internal function used for timeouts */
void _scallion_timeout(int timerid, void* arg);

#endif /* SCALLION_H_ */
