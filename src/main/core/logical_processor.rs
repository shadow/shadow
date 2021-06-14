#![allow(unsafe_op_in_unsafe_fn, dead_code, mutable_transmutes, non_camel_case_types, non_snake_case,
         non_upper_case_globals, unused_assignments, unused_mut)]
#![register_tool(c2rust)]
#![feature(extern_types, register_tool)]

use std::convert::TryInto;
use std::convert::TryFrom;

extern "C" {
    pub type _GAsyncQueue;
    #[no_mangle]
    fn g_async_queue_new() -> *mut GAsyncQueue;
    #[no_mangle]
    fn g_async_queue_unref(queue: *mut GAsyncQueue);
    #[no_mangle]
    fn g_async_queue_try_pop(queue: *mut GAsyncQueue) -> gpointer;
    #[no_mangle]
    fn g_async_queue_push_front(queue: *mut GAsyncQueue, item: gpointer);
    #[no_mangle]
    fn g_free(mem: gpointer);
    #[no_mangle]
    fn g_malloc(n_bytes: gsize) -> gpointer;
    #[no_mangle]
    fn g_malloc_n(n_blocks: gsize, n_block_bytes: gsize) -> gpointer;
    #[no_mangle]
    fn affinity_getGoodWorkerAffinity() -> libc::c_int;
}
pub type size_t = libc::c_ulong;
pub type gsize = libc::c_ulong;
pub type glong = libc::c_long;
pub type gint = libc::c_int;
pub type gpointer = *mut libc::c_void;
pub type GDestroyNotify = Option<unsafe extern "C" fn(_: gpointer) -> ()>;
pub type GAsyncQueue = _GAsyncQueue;
pub struct _LogicalProcessors {
    lps: Vec<LogicalProcessor>,
}
/*

 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
pub type LogicalProcessor = _LogicalProcessor;
pub struct _LogicalProcessor {
    cpuId: libc::c_int,
    readyWorkers: *mut GAsyncQueue,
    doneWorkers: *mut GAsyncQueue,
}
pub type LogicalProcessors = _LogicalProcessors;
#[derive(Copy, Clone)]
#[repr(C)]
pub union C2RustUnnamed {
    pub in_0: *mut libc::c_char,
    pub out: *mut gpointer,
}
#[derive(Copy, Clone)]
#[repr(C)]
pub union C2RustUnnamed_0 {
    pub in_0: *mut libc::c_char,
    pub out: *mut gpointer,
}
unsafe extern "C" fn _idx_to_ptr(mut idx: libc::c_int) -> *mut libc::c_void {
    // g_async_queue stores pointers. We need to use GINT_TO_POINTER to
    // transform the integer to a pointer. Unfortunately that translates 0 to
    // NULL, so we need to offset by 1.
    return (idx + 1 as libc::c_int) as glong as gpointer;
}
unsafe extern "C" fn _ptr_to_idx(mut ptr: *mut libc::c_void) -> libc::c_int {
    // Undo the offset added in _idx_to_ptr.
    return ptr as glong as gint - 1 as libc::c_int;
}
#[no_mangle]
pub unsafe extern "C" fn lps_new(mut n: libc::c_int)
 -> *mut LogicalProcessors {
    let mut lps = Box::new(_LogicalProcessors{ lps: Vec::new()});
    for i in 0..n {
        lps.lps.push(_LogicalProcessor{cpuId: affinity_getGoodWorkerAffinity(),
                                      readyWorkers: g_async_queue_new(),
                                      doneWorkers: g_async_queue_new(),});
    }
    Box::into_raw(lps)
}
#[no_mangle]
pub unsafe extern "C" fn lps_free(mut lps: *mut LogicalProcessors) {
    for lp in &mut (*lps).lps {
        g_async_queue_unref(lp.readyWorkers);
        g_async_queue_unref(lp.doneWorkers);
    }
    Box::from_raw(lps);
}
#[no_mangle]
pub unsafe extern "C" fn lps_n(mut lps: *mut LogicalProcessors)
 -> libc::c_int {
    return (*lps).lps.len() as libc::c_int;
}
#[no_mangle]
pub unsafe extern "C" fn lps_readyPush(mut lps: *mut LogicalProcessors,
                                       mut lpi: libc::c_int,
                                       mut worker: libc::c_int) {
    let lps = lps.as_mut().unwrap();
    let lp  = &mut lps.lps[usize::try_from(lpi).unwrap()];
    g_async_queue_push_front(lp.readyWorkers, _idx_to_ptr(worker));
}
#[no_mangle]
pub unsafe extern "C" fn lps_popWorkerToRunOn(mut lps: *mut LogicalProcessors,
                                              mut lpi: libc::c_int)
 -> libc::c_int {
    let lps = lps.as_mut().unwrap();
    let lp = &mut lps.lps[usize::try_from(lpi).unwrap()];
    let mut i: libc::c_int = 0 as libc::c_int;
    while i < lps_n(lps) {
        // Start with workers that last ran on `lpi`; if none are available
        // steal from another in round-robin order.
        let mut fromLpi: libc::c_int = (lpi + i) % lps_n(lps);
        let mut fromLp = &mut lps.lps[usize::try_from(fromLpi).unwrap()];
        let mut worker: gpointer =
            g_async_queue_try_pop(fromLp.readyWorkers);
        if !worker.is_null() { return _ptr_to_idx(worker) }
        i += 1
    }
    return -(1 as libc::c_int);
}
#[no_mangle]
pub unsafe extern "C" fn lps_donePush(mut lps: *mut LogicalProcessors,
                                      mut lpi: libc::c_int,
                                      mut worker: libc::c_int) {
    let lps = lps.as_mut().unwrap();
    let lp = &mut lps.lps[usize::try_from(lpi).unwrap()];
    // Push to the *front* of the queue so that the last workers to run the
    // current task, which are freshest in cache, are the first ones to run the
    // next task.
    g_async_queue_push_front(lp.doneWorkers, _idx_to_ptr(worker));
}
#[no_mangle]
pub unsafe extern "C" fn lps_finishTask(mut lps: *mut LogicalProcessors) {
    for lp in &mut (*lps).lps {
        // Swap `ready` and `done` Qs
        let mut tmp: *mut GAsyncQueue = lp.readyWorkers;
        lp.readyWorkers = lp.doneWorkers;
        lp.doneWorkers = tmp;
    };
}
#[no_mangle]
pub unsafe extern "C" fn lps_cpuId(mut lps: *mut LogicalProcessors,
                                   mut lpi: libc::c_int) -> libc::c_int {
    let lps = lps.as_mut().unwrap();
    let lp = &mut lps.lps[usize::try_from(lpi).unwrap()];
    // No synchronization needed since cpus are never mutated after
    // construction.
    return lp.cpuId;
}
