/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TABLE_H_
#define SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TABLE_H_

#include <stdbool.h>

#include "main/host/descriptor/descriptor_types.h"

/* Opaque object to store the state needed to implement the module. */
typedef struct _DescriptorTable DescriptorTable;

/* Create an object that can be used to store all descriptors created by
 * a process. The reference count starts at 1; when the table is no longer
 * required, use unref() to release the reference.*/
DescriptorTable* descriptortable_new();

/* Increment the reference count for this table. */
void descriptortable_ref(DescriptorTable* table);

/* Decrement the reference count and free the table if no refs remain. */
void descriptortable_unref(DescriptorTable* table);

/* Store a descriptor object for later reference at the next available index
 * in the table. The chosen table index is stored in the descriptor object and
 * returned. The descriptor is guaranteed to be stored successfully.
 *
 * NOTE: that this consumes a reference to the descriptor, so if you are storing
 * it outside of the descriptor table you will need to ref it after calling
 * this function. */
int descriptortable_add(DescriptorTable* table, LegacyDescriptor* descriptor);

/* Stop storing the descriptor so that it can no longer be referenced. The table
 * index that was used to store the descriptor is cleared from the descriptor
 * and may be assigned to new descriptors that are later added to the table.
 * Returns true if the descriptor was found in the table and removed, and false
 * otherwise.
 *
 * NOTE: this will unref the descriptor which may cause it to be freed. If you
 * still need access to it, you should ref it before calling this function. */
bool descriptortable_remove(DescriptorTable* table, LegacyDescriptor* descriptor);

/* Returns the descriptor at the given table index, or NULL if we are not
 * storing a descriptor at the given index. */
LegacyDescriptor* descriptortable_get(DescriptorTable* table, int index);

/* Store the given descriptor at given index. Any previous descriptor that was
 * stored there will be removed and its table index will be cleared. This
 * unrefs any existing descriptor stored at index as in remove(), and consumes
 * a ref to the existing descriptor as in add(). */
void descriptortable_set(DescriptorTable* table, int index,
                         LegacyDescriptor* descriptor);

/* This is a helper function that handles some corner cases where some
 * descriptors are linked to each other and we must remove that link in
 * order to ensure that the reference count reaches zero and they are properly
 * freed. Otherwise the circular reference will prevent the free operation.
 * TODO: remove this once the TCP layer is better designed. */
void descriptortable_shutdownHelper(DescriptorTable* table);

#endif /* SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TABLE_H_ */
