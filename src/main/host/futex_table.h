/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_FUTEX_TABLE_H_
#define SRC_MAIN_HOST_FUTEX_TABLE_H_

#include <stdbool.h>

/* Opaque object to store the state needed to implement the module. */
typedef struct _FutexTable FutexTable;

#include "main/bindings/c/bindings-opaque.h"
#include "main/host/futex.h"

/* Create an object that can be used to store all futexes created by
 * a host. The reference count starts at 1; when the table is no longer
 * required, use unref() to release the reference.*/
FutexTable* futextable_new();

/* Increment the reference count for this table. */
void futextable_ref(FutexTable* table);

/* Decrement the reference count and free the table if no refs remain. */
void futextable_unref(FutexTable* table);

/* Attempts to store a futex object for later reference at the index corresponding to the unique
 * physical memory address of the futex. Returns true if the index was available and the futex was
 * successfully stored, or false otherwise.
 *
 * NOTE: if this returns true, then it consumes a reference to the futex. If you are also storing
 * futex outside of this table, you will need to ref the futex after calling
 * this function. */
bool futextable_add(FutexTable* table, Futex* futex);

/* Stop storing the futex so that it can no longer be referenced. The table
 * index that was used to store the futex is cleared
 * and may be assigned to new futexes that are later added to the table.
 * Returns true if the futex was found in the table and removed, and false
 * otherwise.
 *
 * NOTE: if this returns true, it will unref the futex which may cause it to be freed. If you
 * still need access to it, you should ref it before calling this function. */
bool futextable_remove(FutexTable* table, Futex* futex);

/* Returns the futex at the given physical address, or NULL if we are not
 * storing a futex at the given address. */
Futex* futextable_get(FutexTable* table, ManagedPhysicalMemoryAddr ptr);

#endif /* SRC_MAIN_HOST_FUTEX_TABLE_H_ */
