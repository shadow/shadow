/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_FUTEX_H_
#define SRC_MAIN_HOST_DESCRIPTOR_FUTEX_H_

typedef struct _Futex Futex;

/* free this with descriptor_free() */
Futex* futex_new();

#endif /* SRC_MAIN_HOST_DESCRIPTOR_FUTEX_H_ */
