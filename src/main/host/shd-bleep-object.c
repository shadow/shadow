/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include <string.h>     // For memcpy
#include <assert.h>     // For assert

#include "shadow.h"

#define SHADOW_GLOBAL_LOCK_COUNT    20

void* shadow_global_entry = NULL;
GMutex shadow_global_entry_lock;
GMutex shadow_global_lock[SHADOW_GLOBAL_LOCK_COUNT];
void init_global_locks() {
    g_mutex_init(&(shadow_global_entry_lock));
    for(int i=0; i<SHADOW_GLOBAL_LOCK_COUNT; i++) {
        g_mutex_init(&(shadow_global_lock[i]));
    }
}
/*      For Process_Emu_ Function       */
void shadow_global_gmutex_lock(int lock_no) {
    assert((lock_no > -1)&&(lock_no < SHADOW_GLOBAL_LOCK_COUNT));
    g_mutex_lock(&(shadow_global_lock[lock_no]));
    return;
}
void shadow_global_gmutex_unlock(int lock_no) {
    assert((lock_no > -1)&&(lock_no < SHADOW_GLOBAL_LOCK_COUNT));
    g_mutex_unlock(&(shadow_global_lock[lock_no]));
    return;
}
void* shadow_lock_try_set_global_entry(void* ptr, size_t sz) {
    void* res;
    g_mutex_lock(&(shadow_global_entry_lock));
    if(shadow_global_entry==NULL) {
        shadow_global_entry = malloc(sz);
        memcpy(shadow_global_entry, ptr, sz);
    }
    res = shadow_global_entry;
    g_mutex_unlock(&(shadow_global_entry_lock));
    return res;
}