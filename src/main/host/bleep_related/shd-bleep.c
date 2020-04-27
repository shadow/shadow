/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include <string.h>     // For memcpy
#include <assert.h>     // For assert

#include "shadow.h"

// BLEEP Shared Entry Functions
#define BLEEP_SHARED_ENTRY_MAX    20
void* bleep_shared_entry[BLEEP_SHARED_ENTRY_MAX];
GMutex bleep_shared_lock[BLEEP_SHARED_ENTRY_MAX];
void shadow_gmutex_lock(int shared_id) {
    assert((shared_id > -1)&&(shared_id < BLEEP_SHARED_ENTRY_MAX));
    g_mutex_lock(&bleep_shared_lock[shared_id]);
    return;
}
void shadow_gmutex_unlock(int shared_id) {
    assert((shared_id > -1)&&(shared_id < BLEEP_SHARED_ENTRY_MAX));
    g_mutex_unlock(&bleep_shared_lock[shared_id]);
    return;
}
void* shadow_claim_shared_entry(void* ptr, size_t sz, int shared_id) {
    void* res;
    shadow_gmutex_lock(shared_id);
    if(bleep_shared_entry[shared_id]==NULL) {
        bleep_shared_entry[shared_id] = malloc(sz);
        memcpy(bleep_shared_entry[shared_id], ptr, sz);
    }
    res = bleep_shared_entry[shared_id];
    shadow_gmutex_unlock(shared_id);
    return res;
}

// BLEEP Virtual ID Functions
GMutex virtual_host_id_lock;
int virtual_host_id = 0;
int shadow_assign_virtual_id() {
    g_mutex_lock(&virtual_host_id_lock);
    gssize ret = virtual_host_id++;
    g_mutex_unlock(&virtual_host_id_lock);
    return ret;
}

// Memory Instrumentation Marker Functions
int g_file_symbol = 0;
int g_line_cnt = 0;
void shadow_instrumentation_marker_set(int file_symbol, int line_cnt) {
    g_file_symbol = file_symbol;
    g_line_cnt = line_cnt;
    return;
}
void shadow_instrumentation_marker_alloc_log(size_t sz) {
    message("SMLA,%d,%d,%d",g_file_symbol, g_line_cnt, sz);
    return;
}
void shadow_instrumentation_marker_free_log(size_t sz) {
    message("SMLF,%d,%d,%d",g_file_symbol, g_line_cnt, sz);
    return;
}

// BLEEP related initialization
void init_bleep_related() {
    // BLEEP Shared Entry Functions
    for(int i=0; i<BLEEP_SHARED_ENTRY_MAX; i++) {
        bleep_shared_entry[i] = NULL;
        g_mutex_init(&bleep_shared_lock[i]);
    }
    // BLEEP Virtual ID Functions
    g_mutex_init(&virtual_host_id_lock);
}