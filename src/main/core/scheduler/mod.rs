// re-export schedulers
pub use thread_per_core::ThreadPerCoreSched;
pub use thread_per_host::ThreadPerHostSched;

mod logical_processor;
mod pools;
mod thread_per_core;
mod thread_per_host;

use std::cell::RefCell;

// any scheduler implementation can read/write the thread-local directly, but external modules can
// only read it using `core_affinity()`

std::thread_local! {
    /// The core affinity of the current thread, as set by the active scheduler.
    static CORE_AFFINITY: RefCell<Option<u32>> = const { RefCell::new(None) };
}

/// Get the core affinity of the current thread, as set by the active scheduler.
pub fn core_affinity() -> Option<u32> {
    CORE_AFFINITY.with(|x| *x.borrow())
}

// the enum supports hosts that satisfy the trait bounds of each scheduler variant
pub trait Host: thread_per_core::Host + thread_per_host::Host {}
impl<T> Host for T where T: thread_per_core::Host + thread_per_host::Host {}

/// A wrapper for different host schedulers. It would have been nice to make this a trait, but would
/// require support for GATs.
pub enum Scheduler<HostType: Host> {
    ThreadPerHost(thread_per_host::ThreadPerHostSched<HostType>),
    ThreadPerCore(thread_per_core::ThreadPerCoreSched<HostType>),
}

impl<HostType: Host> Scheduler<HostType> {
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
        f: impl for<'a, 'b> FnOnce(SchedulerScope<'a, 'b, 'scope, HostType>) + 'scope,
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

pub enum SchedulerScope<'sched, 'pool, 'scope, HostType: Host> {
    ThreadPerHost(thread_per_host::SchedulerScope<'pool, 'scope, HostType>),
    ThreadPerCore(thread_per_core::SchedulerScope<'sched, 'pool, 'scope, HostType>),
}

impl<'sched, 'pool, 'scope, HostType: Host> SchedulerScope<'sched, 'pool, 'scope, HostType> {
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
    pub fn run_with_hosts(self, f: impl Fn(usize, &mut HostIter<HostType>) + Send + Sync + 'scope) {
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
        f: impl Fn(usize, &mut HostIter<HostType>, &T) + Send + Sync + 'scope,
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
pub enum HostIter<'a, 'b, HostType: Host> {
    ThreadPerHost(&'a mut thread_per_host::HostIter<HostType>),
    ThreadPerCore(&'a mut thread_per_core::HostIter<'b, HostType>),
}

impl<'a, 'b, HostType: Host> HostIter<'a, 'b, HostType> {
    /// For each [`Host`], calls `f` with each `Host`. The `Host` must be returned by the closure.
    pub fn for_each<F>(&mut self, f: F)
    where
        F: FnMut(HostType) -> HostType,
    {
        match self {
            Self::ThreadPerHost(x) => x.for_each(f),
            Self::ThreadPerCore(x) => x.for_each(f),
        }
    }
}
