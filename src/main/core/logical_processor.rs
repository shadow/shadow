#![allow(unsafe_op_in_unsafe_fn, dead_code, mutable_transmutes, non_camel_case_types, non_snake_case,
         non_upper_case_globals, unused_assignments, unused_mut)]
#![register_tool(c2rust)]
#![feature(extern_types, register_tool)]

use std::convert::TryFrom;
use crossbeam::queue::SegQueue;

extern "C" {
    #[no_mangle]
    fn affinity_getGoodWorkerAffinity() -> libc::c_int;
}
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
    readyWorkers: SegQueue<libc::c_int>,
    doneWorkers: SegQueue<libc::c_int>,
}
pub type LogicalProcessors = _LogicalProcessors;
#[no_mangle]
pub unsafe extern "C" fn lps_new(mut n: libc::c_int)
 -> *mut LogicalProcessors {
    let mut lps = Box::new(_LogicalProcessors{ lps: Vec::new()});
    for i in 0..n {
        lps.lps.push(_LogicalProcessor{cpuId: affinity_getGoodWorkerAffinity(),
                                      readyWorkers: SegQueue::new(),
                                      doneWorkers: SegQueue::new(),});
    }
    Box::into_raw(lps)
}
#[no_mangle]
pub unsafe extern "C" fn lps_free(mut lps: *mut LogicalProcessors) {
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
    lp.readyWorkers.push(worker);
}
#[no_mangle]
pub unsafe extern "C" fn lps_popWorkerToRunOn(mut lps: *mut LogicalProcessors,
                                              mut lpi: libc::c_int)
 -> libc::c_int {
    let lps = lps.as_mut().unwrap();
    let lpi = usize::try_from(lpi).unwrap();
    let lp = &mut lps.lps[lpi];
    for i in 0..lps.lps.len() {
        // Start with workers that last ran on `lpi`; if none are available
        // steal from another in round-robin order.
        let mut fromLpi = (lpi + i) % lps.lps.len();
        let mut fromLp = &mut lps.lps[fromLpi];
        if let Some(worker) = fromLp.readyWorkers.pop() {
            return worker
        }
    }
    return -1
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
    lp.doneWorkers.push(worker);
}
#[no_mangle]
pub unsafe extern "C" fn lps_finishTask(mut lps: *mut LogicalProcessors) {
    for lp in &mut (*lps).lps {
        std::mem::swap(&mut lp.readyWorkers, &mut lp.doneWorkers);
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
