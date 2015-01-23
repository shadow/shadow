/*
 * See LICENSE for licensing information
 */

#include "glib.h"

G_LOCK_DEFINE_STATIC(tgen_global_lock);

void tgen_lock() {
    G_LOCK(tgen_global_lock);
}

void tgen_unlock() {
    G_UNLOCK(tgen_global_lock);
}
