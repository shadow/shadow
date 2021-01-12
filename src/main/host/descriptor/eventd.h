/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_EVENTD_H_
#define SRC_MAIN_HOST_DESCRIPTOR_EVENTD_H_

#include <stdbool.h>
#include <unistd.h>

typedef struct _EventD EventD;

/* free this with descriptor_free() */
EventD* eventd_new(unsigned int counter_init_val, bool is_semaphore);

ssize_t eventd_read(EventD* eventfd, void* buf, size_t buflen);
ssize_t eventd_write(EventD* eventfd, const void* buf, size_t buflen);

#endif /* SRC_MAIN_HOST_DESCRIPTOR_EVENTD_H_ */
