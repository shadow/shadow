/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_DRIVER_H_
#define SHD_TGEN_DRIVER_H_

/* opaque struct containing trafficgenerator data */
typedef struct _TGenDriver TGenDriver;

TGenDriver* tgendriver_new(gint argc, gchar* argv[], ShadowLogFunc logf);
void tgendriver_ref(TGenDriver* driver);
void tgendriver_unref(TGenDriver* driver);

void tgendriver_activate(TGenDriver* driver);

gboolean tgendriver_hasEnded(TGenDriver* driver);
gint tgendriver_getEpollDescriptor(TGenDriver* driver);

void tgendriver_shutdown(TGenDriver* driver);

#endif /* SHD_TGEN_DRIVER_H_ */
