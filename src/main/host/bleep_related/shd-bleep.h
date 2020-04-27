/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_BLEEP_H_
#define SHD_BLEEP_H_

#include "shadow.h"

// BLEEP Shared Entry Functions
void shadow_gmutex_lock(int shared_id);
void shadow_gmutex_unlock(int shared_id);
void* shadow_claim_shared_entry(void* ptr, size_t sz, int shared_id);
// BLEEP Virtual ID Functions
int shadow_assign_virtual_id();
// BLEEP related initialization
void init_bleep_related();

#endif /* SHD_BLEEP_OBJECT_H_ */