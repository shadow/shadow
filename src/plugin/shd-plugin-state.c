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

PluginState* pluginstate_new(PluginFunctionTable* callbackFunctions, guint nVariables, va_list vargs) {
	g_assert(callbackFunctions);
	PluginState* state = g_new0(PluginState, 1);
	MAGIC_INIT(state);

	/* store the pointers to the callbacks the plugin wants us to call */
	state->functions = g_new0(PluginFunctionTable, 1);
	*(state->functions) = *callbackFunctions;

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

		DataEntry* entry = dataentry_new(reference, size);
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

	copyState->functions = g_new0(PluginFunctionTable, 1);
	*(copyState->functions) = *(state->functions);

	copyState->dataEntries = NULL;

	/*
	 * to avoid traversing entire list when appending, we prepend the items and
	 * then reverse the list. anyway, order probably doesn't matter.
	 */
	GSList* next = state->dataEntries;
	while(next) {
		DataEntry* entry = next->data;
		DataEntry* copyEntry = dataentry_copyNew(entry);

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

	/* if the above asserts pass, these are unnecessary */
	destinationState->totalEntrySize = sourceState->totalEntrySize;
	destinationState->nEntries = sourceState->nEntries;

	*(destinationState->functions) = *(sourceState->functions);

	/* go through and copy each entry */
	GSList* nextSource = sourceState->dataEntries;
	GSList* nextDestination = destinationState->dataEntries;
	while(nextSource && nextDestination) {
		dataentry_copy(nextSource->data, nextDestination->data);

		nextSource = g_slist_next(nextSource);
		nextDestination = g_slist_next(nextDestination);
	}
}

void pluginstate_free(PluginState* state) {
	MAGIC_ASSERT(state);

	g_free(state->functions);
	g_slist_free_full(state->dataEntries, dataentry_free);

	MAGIC_CLEAR(state);
	g_free(state);
}
