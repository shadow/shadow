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

#include <stdlib.h>
#include <string.h>

#include "reference_counter.h"

rc_object_tp rc_create(void* data, rc_object_destructor_fp destructor) {
	rc_object_tp rc_object = malloc(sizeof(rc_object_t));
	rc_object->data = data;
	rc_object->destructor = destructor;
	rc_object->reference_count = 1;
	rc_object->isSet = 0;
	return rc_object;
}

void* rc_get(rc_object_tp rc_object) {
	if(rc_object != NULL && rc_object->reference_count > 0) {
		return rc_object->data;
	}
	return NULL;
}

void rc_retain(rc_object_tp rc_object) {
	if(rc_object != NULL) {
		rc_object->reference_count++;
	}
}

void rc_release(rc_object_tp rc_object) {
	if(rc_object != NULL && --rc_object->reference_count == 0) {
		int breakpoint = 0;
		if(rc_object->isSet) {
			breakpoint++;
		}
		if(rc_object->destructor != NULL) {
			(*rc_object->destructor)(rc_object->data);
		}
		memset(rc_object, 0, sizeof(rc_object_t));
		free(rc_object);
	}
}

void rc_set(rc_object_tp rc_object) {
	if(rc_object != NULL) {
		rc_object->isSet = 1;
	}
}

void rc_unset(rc_object_tp rc_object) {
	if(rc_object != NULL) {
		rc_object->isSet = 0;
	}
}
