/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_IO_H_
#define SHD_TGEN_IO_H_

#include "shd-tgen.h"

typedef enum _TGenEvent {
    TGEN_EVENT_NONE = 0,
    TGEN_EVENT_READ = 1 << 0,
    TGEN_EVENT_WRITE = 1 << 1,
    TGEN_EVENT_DONE = 1 << 2,
} TGenEvent;

typedef struct _TGenIO TGenIO;

typedef TGenEvent (*TGenIO_onEventFunc)(gpointer data, gint descriptor, TGenEvent events);

TGenIO* tgenio_new();
void tgenio_ref(TGenIO* io);
void tgenio_unref(TGenIO* io);

gboolean tgenio_register(TGenIO* io, gint descriptor, TGenIO_onEventFunc notify, gpointer notifyData);
void tgenio_loopOnce(TGenIO* io);

gint tgenio_getEpollDescriptor(TGenIO* io);
guint tgenio_getSize(TGenIO* io);

#endif /* SHD_TGEN_IO_H_ */
