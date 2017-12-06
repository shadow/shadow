/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_DRIVER_H_
#define SHD_TGEN_DRIVER_H_

#include "shd-tgen.h"

/* opaque struct containing trafficgenerator data */
typedef struct _TGenDriver TGenDriver;

TGenDriver* tgendriver_new(TGenGraph* graph);
void tgendriver_ref(TGenDriver* driver);
void tgendriver_unref(TGenDriver* driver);

void tgendriver_activate(TGenDriver* driver);

gboolean tgendriver_hasEnded(TGenDriver* driver);
gint tgendriver_getEpollDescriptor(TGenDriver* driver);
void tgendriver_registerTransferPause(TGenDriver *driver, TGenTimer *timer);
void tgendriver_setEvents(TGenDriver *driver, gint descriptor, TGenEvent events);

#endif /* SHD_TGEN_DRIVER_H_ */
