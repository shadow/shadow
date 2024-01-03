/*
* The Shadow Simulator
* Copyright (c) 2010-2011, Rob Jansen
* See LICENSE for licensing information
*/
use crossbeam::queue::ArrayQueue;

/// A set of `n` logical processors.
pub struct LogicalProcessors {
    lps: Vec<LogicalProcessor>,
}

impl LogicalProcessors {
    pub fn new(processors: &[Option<u32>], num_workers: usize) -> Self {
        let mut lps = Vec::new();

        for cpu_id in processors {
            lps.push(LogicalProcessor {
                cpu_id: *cpu_id,
                // each queue must be large enough to store all the workers
                ready_workers: ArrayQueue::new(num_workers),
                done_workers: ArrayQueue::new(num_workers),
            });
        }

        Self { lps }
    }

    /// Add a worker id to be run on processor `lpi`.
    pub fn add_worker(&self, lpi: usize, worker: usize) {
        self.lps[lpi].ready_workers.push(worker).unwrap();
    }

    /// Get a worker id to run on processor `lpi`. Returns `None` if there are no more workers to run.
    pub fn next_worker(&self, lpi: usize) -> Option<(usize, usize)> {
        // Start with workers that last ran on `lpi`; if none are available steal from another in
        // round-robin order.
        for (from_lpi, from_lp) in self
            .lps
            .iter()
            .enumerate()
            .cycle()
            .skip(lpi)
            .take(self.lps.len())
        {
            if let Some(worker) = from_lp.ready_workers.pop() {
                // Mark the worker as "done"; push the worker to `lpi`, not the processor that it
                // was stolen from.
                self.lps[lpi].done_workers.push(worker).unwrap();

                return Some((worker, from_lpi));
            }
        }

        None
    }

    /// Call after finishing running a task on all workers to mark all workers ready to run again.
    pub fn reset(&mut self) {
        for lp in &mut self.lps {
            assert!(lp.ready_workers.is_empty(), "Not all workers were used");
            std::mem::swap(&mut lp.ready_workers, &mut lp.done_workers);
        }
    }

    /// Returns the cpu id that should be used with [`libc::sched_setaffinity`] to run a thread on
    /// `lpi`. Returns `None` if no cpu id was assigned to `lpi`.
    pub fn cpu_id(&self, lpi: usize) -> Option<u32> {
        self.lps[lpi].cpu_id
    }

    /// Returns an iterator of logical processor indexes.
    pub fn iter(&self) -> impl std::iter::ExactSizeIterator<Item = usize> + Clone {
        0..self.lps.len()
    }
}

pub struct LogicalProcessor {
    cpu_id: Option<u32>,
    ready_workers: ArrayQueue<usize>,
    done_workers: ArrayQueue<usize>,
}
