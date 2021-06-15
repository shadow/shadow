/*

* The Shadow Simulator
* Copyright (c) 2010-2011, Rob Jansen
* See LICENSE for licensing information
*/
use crossbeam::queue::SegQueue;
use std::convert::TryFrom;
use std::convert::TryInto;

extern "C" {
    fn affinity_getGoodWorkerAffinity() -> libc::c_int;
}

pub struct LogicalProcessors {
    lps: Vec<LogicalProcessor>,
}

impl LogicalProcessors {
    pub fn new(n: usize) -> Self {
        let mut lps = Vec::new();
        for _ in 0..n {
            lps.push(LogicalProcessor {
                cpu_id: unsafe { affinity_getGoodWorkerAffinity() },
                ready_workers: SegQueue::new(),
                done_workers: SegQueue::new(),
            });
        }
        Self { lps }
    }

    pub fn ready_push(&mut self, lpi: usize, worker: usize) {
        self.lps[lpi].ready_workers.push(worker);
    }

    pub fn pop_worker_to_run_on(&mut self, lpi: usize) -> Option<usize> {
        for i in 0..self.lps.len() {
            // Start with workers that last ran on `lpi`; if none are available
            // steal from another in round-robin order.
            let from_lpi = (lpi + i) % self.lps.len();
            let from_lp = &mut self.lps[from_lpi];
            if let Some(worker) = from_lp.ready_workers.pop() {
                return Some(worker);
            }
        }
        return None;
    }

    pub fn done_push(&mut self, lpi: usize, worker: usize) {
        self.lps[lpi].done_workers.push(worker);
    }

    pub fn finish_task(&mut self) {
        for lp in &mut self.lps {
            std::mem::swap(&mut lp.ready_workers, &mut lp.done_workers);
        }
    }

    pub fn cpu_id(&self, lpi: usize) -> libc::c_int {
        self.lps[lpi].cpu_id
    }
}

pub struct LogicalProcessor {
    cpu_id: libc::c_int,
    ready_workers: SegQueue<usize>,
    done_workers: SegQueue<usize>,
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn lps_new(n: libc::c_int) -> *mut LogicalProcessors {
        Box::into_raw(Box::new(LogicalProcessors::new(n.try_into().unwrap())))
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_free(lps: *mut LogicalProcessors) {
        unsafe { Box::from_raw(lps) };
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_n(lps: *mut LogicalProcessors) -> libc::c_int {
        return unsafe { lps.as_mut() }.unwrap().lps.len() as libc::c_int;
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_readyPush(
        lps: *mut LogicalProcessors,
        lpi: libc::c_int,
        worker: libc::c_int,
    ) {
        unsafe { lps.as_mut() }.unwrap().ready_push(
            usize::try_from(lpi).unwrap(),
            usize::try_from(worker).unwrap(),
        );
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_popWorkerToRunOn(
        lps: *mut LogicalProcessors,
        lpi: libc::c_int,
    ) -> libc::c_int {
        match unsafe { lps.as_mut() }
            .unwrap()
            .pop_worker_to_run_on(lpi.try_into().unwrap())
        {
            Some(w) => w.try_into().unwrap(),
            None => -1,
        }
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_donePush(
        lps: *mut LogicalProcessors,
        lpi: libc::c_int,
        worker: libc::c_int,
    ) {
        unsafe { lps.as_mut() }
            .unwrap()
            .done_push(lpi.try_into().unwrap(), worker.try_into().unwrap());
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_finishTask(lps: *mut LogicalProcessors) {
        unsafe { lps.as_mut() }.unwrap().finish_task();
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_cpuId(
        lps: *mut LogicalProcessors,
        lpi: libc::c_int,
    ) -> libc::c_int {
        unsafe { lps.as_mut() }
            .unwrap()
            .cpu_id(lpi.try_into().unwrap())
    }
}
