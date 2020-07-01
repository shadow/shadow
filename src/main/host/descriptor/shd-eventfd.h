/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_EVENTFD_H_
#define SHD_EVENTFD_H_

#include "shadow.h"

typedef struct _EventFD EventFD;

/* free this with descriptor_free() */
EventFD* eventfd_new(gint handle, gint flags);
void eventfd_setInitVal(EventFD* eventfd, int initval);
ssize_t shd_eventfd_read(EventFD* eventfd, void *buf, size_t count);
ssize_t shd_eventfd_write(EventFD* eventfd, const void *buf, size_t count);


#endif /* SHD_EVENTFD_H_ */
