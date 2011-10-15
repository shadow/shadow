/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include "shadow.h"
#include <string.h>

DataEntry* dataentry_new(gpointer reference, gsize size) {
	DataEntry* entry = g_new0(DataEntry, 1);
	MAGIC_INIT(entry);

	entry->reference = reference;
	entry->size = size;

	return entry;
}

DataEntry* dataentry_copyNew(DataEntry* entry) {
	MAGIC_ASSERT(entry);
	gpointer copy = g_slice_copy(entry->size, entry->reference);
	return dataentry_new(copy, entry->size);
}

void dataentry_copy(DataEntry* sourceEntry, DataEntry* destinationEntry) {
	MAGIC_ASSERT(sourceEntry);
	MAGIC_ASSERT(destinationEntry);

	g_assert(destinationEntry->size == sourceEntry->size);
	g_assert(destinationEntry->reference && sourceEntry->reference);

	memmove(destinationEntry->reference, sourceEntry->reference, destinationEntry->size);
}

void dataentry_free(gpointer data) {
	DataEntry* entry = (DataEntry*) data;
	MAGIC_ASSERT(entry);

	MAGIC_CLEAR(entry);
	g_free(entry);
}
