/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

/* Holds pointers to data for each variable registered by a plug-in */
struct _PluginState {
	PluginFunctionTable* functions;
	GSList* dataEntries;
	gsize nEntries;
	gsize totalEntrySize;
	MAGIC_DECLARE;
};

/* internal structure to manage state entries */
typedef struct _PluginStateEntry PluginStateEntry;
struct _PluginStateEntry {
	gpointer reference;
	gsize size;
	gboolean isCopy;
	MAGIC_DECLARE;
};

static PluginStateEntry* _pluginstateentry_new(gpointer reference, gsize size, gboolean isCopy) {
	PluginStateEntry* entry = g_new0(PluginStateEntry, 1);
	MAGIC_INIT(entry);

	debug("plugin copied %lu bytes at %p", size, reference);

	entry->reference = reference;
	entry->size = size;
	entry->isCopy = isCopy;

	return entry;
}

static PluginStateEntry* _pluginstateentry_copyNew(PluginStateEntry* entry) {
	MAGIC_ASSERT(entry);
	gpointer copy = g_slice_copy(entry->size, entry->reference);
	/* we copied the reference, so we need to free it later */
	return _pluginstateentry_new(copy, entry->size, TRUE);
}

static void _pluginstateentry_copy(PluginStateEntry* sourceEntry, PluginStateEntry* destinationEntry) {
	MAGIC_ASSERT(sourceEntry);
	MAGIC_ASSERT(destinationEntry);

	g_assert(destinationEntry->size == sourceEntry->size);
	g_assert(destinationEntry->reference && sourceEntry->reference);

	g_memmove(destinationEntry->reference, sourceEntry->reference, destinationEntry->size);
}

static void _pluginstateentry_free(PluginStateEntry* entry) {
	MAGIC_ASSERT(entry);

	if(entry->isCopy) {
		g_slice_free1(entry->size, entry->reference);
	}

	MAGIC_CLEAR(entry);
	g_free(entry);
}

PluginState* pluginstate_new(guint nVariables, va_list vargs) {
	PluginState* state = g_new0(PluginState, 1);
	MAGIC_INIT(state);

	/* initialize the empty singly-linked list of variables */
	state->dataEntries = NULL;

	/* go through each physical address and size and save it */
	va_list variables;
	G_VA_COPY(variables, vargs);

	/* to avoid traversing entire list when appending, we prepend the items and
	 * then reverse the list. anyway, order probably doesn't matter.
	 */
	for(gint i = 0; i < nVariables; i++) {
		gsize size = va_arg(variables, gsize);
		gpointer reference = va_arg(variables, gpointer);

		/* we dont own reference, so dont free it later */
		PluginStateEntry* entry = _pluginstateentry_new(reference, size, FALSE);
		state->dataEntries = g_slist_prepend(state->dataEntries, entry);
		(state->nEntries)++;
		state->totalEntrySize += size;
	}

	state->dataEntries = g_slist_reverse(state->dataEntries);

	va_end(variables);

	return state;
}

PluginState* pluginstate_copyNew(PluginState* state) {
	MAGIC_ASSERT(state);

	PluginState* copyState = g_new0(PluginState, 1);
	MAGIC_INIT(copyState);

	copyState->dataEntries = NULL;

	/*
	 * to avoid traversing entire list when appending, we prepend the items and
	 * then reverse the list. anyway, order probably doesn't matter.
	 */
	GSList* next = state->dataEntries;
	while(next) {
		PluginStateEntry* entry = next->data;
		PluginStateEntry* copyEntry = _pluginstateentry_copyNew(entry);

		copyState->dataEntries = g_slist_prepend(copyState->dataEntries, copyEntry);
		(copyState->nEntries)++;
		copyState->totalEntrySize += copyEntry->size;

		next = g_slist_next(next);
	}
	copyState->dataEntries = g_slist_reverse(copyState->dataEntries);

	return copyState;
}

void pluginstate_copy(PluginState* sourceState, PluginState* destinationState) {
	MAGIC_ASSERT(sourceState);
	MAGIC_ASSERT(destinationState);

	/* if swapping state, the number of entries and size of each MUST match */
	g_assert(sourceState->nEntries == destinationState->nEntries);
	g_assert(sourceState->totalEntrySize == destinationState->totalEntrySize);

	/* go through and copy each entry */
	gint numEntriesCopied = 0;
	GSList* nextSource = sourceState->dataEntries;
	GSList* nextDestination = destinationState->dataEntries;
	while(nextSource && nextDestination) {
		_pluginstateentry_copy(nextSource->data, nextDestination->data);

		numEntriesCopied++;
		nextSource = g_slist_next(nextSource);
		nextDestination = g_slist_next(nextDestination);
	}

	g_assert(numEntriesCopied == sourceState->nEntries);
}

void pluginstate_free(PluginState* state) {
	MAGIC_ASSERT(state);

	g_slist_free_full(state->dataEntries, (GDestroyNotify)_pluginstateentry_free);

	MAGIC_CLEAR(state);
	g_free(state);
}
