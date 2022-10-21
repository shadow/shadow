pub mod runahead;

// re-export schedulers
pub use thread_per_core::ThreadPerCoreSched;
pub use thread_per_host::ThreadPerHostSched;

mod logical_processor;
pub mod pools;
mod thread_per_core;
mod thread_per_host;

use std::cell::RefCell;

use crate::host::host::HostRef;

// any scheduler implementation can read/write the thread-local directly, but external modules can
// only read it using `core_affinity()`

std::thread_local! {
    /// The core affinity of the current thread, as set by the active scheduler.
    static CORE_AFFINITY: RefCell<Option<u32>> = RefCell::new(None);
}

/// Get the core affinity of the current thread, as set by the active scheduler.
pub fn core_affinity() -> Option<u32> {
    CORE_AFFINITY.with(|x| *x.borrow())
}

/// A wrapper for different host schedulers. It would have been nice to make this a trait, but would
/// require support for GATs.
pub enum Scheduler {
    ThreadPerHost(thread_per_host::ThreadPerHostSched),
    ThreadPerCore(thread_per_core::ThreadPerCoreSched),
}

impl Scheduler {
    /// The maximum number of threads that will ever be run in parallel.
    pub fn parallelism(&self) -> usize {
        match self {
            Self::ThreadPerHost(sched) => sched.parallelism(),
            Self::ThreadPerCore(sched) => sched.parallelism(),
        }
    }

    /// A scope for any task run on the scheduler. The current thread will block at the end of the
    /// scope until the task has completed.
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a, 'b> FnOnce(SchedulerScope<'a, 'b, 'scope>) + 'scope,
    ) {
        match self {
            Self::ThreadPerHost(sched) => sched.scope(move |s| f(SchedulerScope::ThreadPerHost(s))),
            Self::ThreadPerCore(sched) => sched.scope(move |s| f(SchedulerScope::ThreadPerCore(s))),
        }
    }

    /// Join all threads started by the scheduler.
    pub fn join(self) {
        match self {
            Self::ThreadPerHost(sched) => sched.join(),
            Self::ThreadPerCore(sched) => sched.join(),
        }
    }
}

pub enum SchedulerScope<'sched, 'pool, 'scope> {
    ThreadPerHost(thread_per_host::SchedulerScope<'pool, 'scope>),
    ThreadPerCore(thread_per_core::SchedulerScope<'sched, 'pool, 'scope>),
}

impl<'sched, 'pool, 'scope> SchedulerScope<'sched, 'pool, 'scope> {
    /// Run the closure on all threads. The closure is given an index of the currently running
    /// thread.
    pub fn run(self, f: impl Fn(usize) + Sync + Send + 'scope) {
        match self {
            Self::ThreadPerHost(scope) => scope.run(f),
            Self::ThreadPerCore(scope) => scope.run(f),
        }
    }

    /// Run the closure on all threads. The closure is given an index of the currently running
    /// thread and a host iterator.
    ///
    /// The closure must iterate over the provided `HostIter` to completion (until `next()` returns
    /// `None`), otherwise this may panic. The host iterator is not a real [`std::iter::Iterator`],
    /// but rather a fake iterator that behaves like a streaming iterator.
    pub fn run_with_hosts(self, f: impl Fn(usize, &mut HostIter) + Send + Sync + 'scope) {
        match self {
            Self::ThreadPerHost(scope) => scope.run_with_hosts(move |idx, iter| {
                let mut iter = HostIter::ThreadPerHost(iter);
                f(idx, &mut iter)
            }),
            Self::ThreadPerCore(scope) => scope.run_with_hosts(move |idx, iter| {
                let mut iter = HostIter::ThreadPerCore(iter);
                f(idx, &mut iter)
            }),
        }
    }

    /// Run the closure on all threads. The closure is given an index of the currently running
    /// thread, a host iterator, and an element of `data`.
    ///
    /// The closure must iterate over the provided `HostIter` to completion (until `next()` returns
    /// `None`), otherwise this may panic. The host iterator is not a real [`std::iter::Iterator`],
    /// but rather a fake iterator that behaves like a streaming iterator.
    ///
    /// Each call of the closure will be given an element of `data`, and this element will not be
    /// given to any other thread while this closure is running, which means you should not expect
    /// any contention on this element if using interior mutability.  The provided slice **must**
    /// have a length of at least [`Scheduler::parallelism`]. If the data needs to be initialized,
    /// it should be initialized before calling this function and not at the beginning of the
    /// closure. The element may be given to multiple threads, but never two threads at the same
    /// time.
    pub fn run_with_data<T>(
        self,
        data: &'scope [T],
        f: impl Fn(usize, &mut HostIter, &T) + Send + Sync + 'scope,
    ) where
        T: Sync,
    {
        match self {
            Self::ThreadPerHost(scope) => scope.run_with_data(data, move |idx, iter, elem| {
                let mut iter = HostIter::ThreadPerHost(iter);
                f(idx, &mut iter, elem)
            }),
            Self::ThreadPerCore(scope) => scope.run_with_data(data, move |idx, iter, elem| {
                let mut iter = HostIter::ThreadPerCore(iter);
                f(idx, &mut iter, elem)
            }),
        }
    }
}

/// Supports iterating over all hosts assigned to this thread.
pub enum HostIter<'a, 'b> {
    ThreadPerHost(&'a mut thread_per_host::HostIter<'b>),
    ThreadPerCore(&'a mut thread_per_core::HostIter<'b>),
}

impl<'a, 'b> HostIter<'a, 'b> {
    /// Get the next host.
    pub fn next(&mut self) -> Option<&mut HostRef> {
        match self {
            Self::ThreadPerHost(x) => x.next(),
            Self::ThreadPerCore(x) => x.next(),
        }
    }
}

mod export {
    use super::*;

    /// Get the core affinity of the current thread, as set by the active scheduler. Returns `-1` if
    /// the affinity is not set.
    #[no_mangle]
    pub extern "C" fn scheduler_getAffinity() -> i32 {
        core_affinity()
            .map(|x| i32::try_from(x).unwrap())
            .unwrap_or(-1)
    }
}
