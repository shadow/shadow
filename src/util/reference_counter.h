/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef REFERENCE_COUNTER_H_
#define REFERENCE_COUNTER_H_

#include <stdint.h>

/*
 * Implements a generic reference counter for objects that destroys the object
 * when it has no more references. In general, rc_retain should be called any
 * time a pointer to the rc_object is stored, and rc_release should be called
 * when a pointer is deleted. This includes stack pointers, so functions that
 * take in a rc_object pointer should immediately call rc_retain, and
 * should call rc_release right before returning (unless a pointer to the
 * rc_object itself is returned, in which case rc_release is not called upon
 * exiting a function, and the caller of said function is responsible to handle
 * proper releases).
 */

typedef void (*rc_object_destructor_fp)(void* object);

typedef struct rc_object_s {
	void* data;
	uint8_t reference_count;
	rc_object_destructor_fp destructor;
}rc_object_t, *rc_object_tp;

rc_object_tp rc_create(void* data, rc_object_destructor_fp destructor);
void* rc_get(rc_object_tp rc_object);
void rc_retain(rc_object_tp rc_object);
void rc_release(rc_object_tp rc_object);

#endif /* REFERENCE_COUNTER_H_ */
