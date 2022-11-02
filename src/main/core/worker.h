/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_WORKER_H_
#define SHD_WORKER_H_

#include "main/bindings/c/bindings.h"

// Increment a counter for the allocation of the object with the given name.
// This should be paired with an increment of the dealloc counter with the
// same name, otherwise we print a warning that a memory leak was detected.
#define worker_count_allocation(type) worker_increment_object_alloc_counter(#type)

// Increment a counter for the deallocation of the object with the given name.
// This should be paired with an increment of the alloc counter with the
// same name, otherwise we print a warning that a memory leak was detected.
#define worker_count_deallocation(type) worker_increment_object_dealloc_counter(#type)

#endif /* SHD_WORKER_H_ */
