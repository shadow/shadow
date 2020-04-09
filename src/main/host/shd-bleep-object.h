/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_BLEEP_H_
#define SHD_BLEEP_H_

#include "shadow.h"

/* global object share */
void init_global_locks();
void shadow_global_gmutex_lock(int lock_no);
void shadow_global_gmutex_unlock(int lock_no);
void* shadow_lock_try_set_global_entry(void* ptr, size_t sz);
/* random id assignment per call */
void init_virtual_host_id_lock();
int shadow_assign_virtual_id();

#endif /* SHD_BLEEP_OBJECT_H_ */
