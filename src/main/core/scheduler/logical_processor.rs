/*
* The Shadow Simulator
* Copyright (c) 2010-2011, Rob Jansen
* See LICENSE for licensing information
*/
use crate::cshadow;
use crate::utility::notnull::*;
use crossbeam::queue::SegQueue;

/// A set of `n` logical processors
pub struct LogicalProcessors {
    lps: Vec<LogicalProcessor>,
}

impl LogicalProcessors {
    pub fn new(n: usize) -> Self {
        let mut lps = Vec::new();
        for _ in 0..n {
            lps.push(LogicalProcessor {
                cpu_id: unsafe { cshadow::affinity_getGoodWorkerAffinity() },
                ready_workers: SegQueue::new(),
                done_workers: SegQueue::new(),
            });
        }
        Self { lps }
    }

    /// Add a worker to be run on `lpi`.
    pub fn ready_push(&self, lpi: usize, worker: usize) {
        self.lps[lpi].ready_workers.push(worker);
    }

    /// Get a worker ID to run on `lpi`. Returns None if there are no more
    /// workers to run.
    pub fn pop_worker_to_run_on(&self, lpi: usize) -> Option<usize> {
        for i in 0..self.lps.len() {
            // Start with workers that last ran on `lpi`; if none are available
            // steal from another in round-robin order.
            let from_lpi = (lpi + i) % self.lps.len();
            let from_lp = &self.lps[from_lpi];
            if let Some(worker) = from_lp.ready_workers.pop() {
                return Some(worker);
            }
        }
        None
    }

    /// Record that the `worker` previously returned by `lp_readyPopFor` has
    /// completed its task.
    pub fn done_push(&self, lpi: usize, worker: usize) {
        self.lps[lpi].done_workers.push(worker);
    }

    /// Call after finishing running a task on all workers to mark all workers ready
    /// to run again.
    pub fn finish_task(&mut self) {
        for lp in &mut self.lps {
            std::mem::swap(&mut lp.ready_workers, &mut lp.done_workers);
        }
    }

    /// Returns the cpu id that should be used with the `affinity_*` module to
    /// run a thread on `lpi`
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
        unsafe { Box::from_raw(notnull_mut_debug(lps)) };
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_n(lps: *const LogicalProcessors) -> libc::c_int {
        return unsafe { lps.as_ref() }.unwrap().lps.len() as libc::c_int;
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_readyPush(
        lps: *const LogicalProcessors,
        lpi: libc::c_int,
        worker: libc::c_int,
    ) {
        unsafe { lps.as_ref() }.unwrap().ready_push(
            usize::try_from(lpi).unwrap(),
            usize::try_from(worker).unwrap(),
        );
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_popWorkerToRunOn(
        lps: *const LogicalProcessors,
        lpi: libc::c_int,
    ) -> libc::c_int {
        match unsafe { lps.as_ref() }
            .unwrap()
            .pop_worker_to_run_on(lpi.try_into().unwrap())
        {
            Some(w) => w.try_into().unwrap(),
            None => -1,
        }
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_donePush(
        lps: *const LogicalProcessors,
        lpi: libc::c_int,
        worker: libc::c_int,
    ) {
        unsafe { lps.as_ref() }
            .unwrap()
            .done_push(lpi.try_into().unwrap(), worker.try_into().unwrap());
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_finishTask(lps: *mut LogicalProcessors) {
        unsafe { lps.as_mut() }.unwrap().finish_task();
    }
    #[no_mangle]
    pub unsafe extern "C" fn lps_cpuId(
        lps: *const LogicalProcessors,
        lpi: libc::c_int,
    ) -> libc::c_int {
        unsafe { lps.as_ref() }
            .unwrap()
            .cpu_id(lpi.try_into().unwrap())
    }
}
