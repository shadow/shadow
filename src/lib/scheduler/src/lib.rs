//! Scheduler for Shadow discrete-event simulations.
//!
//! In Shadow, each host has a queue of events it must process, and within a given scheduling round
//! the host can process these events independently of all other hosts. This means that Shadow can
//! process each host in parallel.
//!
//! For a given list of hosts, the scheduler must tell each host to run and process its events. This
//! must occur in parallel with minimal overhead. With a typical thread pool you might create a new
//! task for each host and run all of the tasks on the thread pool, but this is too slow for Shadow
//! and results in a huge runtime performance loss (simulation run time increases by over 10x). Most
//! thread pools also don't have a method of specifying which task (and therefore which host) runs
//! on which CPU core, which is an important performance optimization on NUMA architectures.
//!
//! The scheduler in this library uses a thread pool optimized for running the same task across all
//! threads. This means that the scheduler takes a single function/closure and runs it on each
//! thread simultaneously (and sometimes repeatedly) until all of the hosts have been processed. The
//! implementation details depend on which scheduler is in use ( [`ThreadPerCoreSched`] or
//! [`ThreadPerHostSched`]), but all schedulers share a common interface so that they can easily be
//! switched out.
//!
//! The [`Scheduler`] provides a simple wrapper to make it easier to support both schedulers, which
//! is useful if you want to choose one at runtime. The schedulers use a "[scoped
//! threads][std::thread::scope]" design to simplify the calling code. This helps the calling code
//! share data with the scheduler without requiring the caller to use locking or "unsafe" to do so.
//!
//! ```
//! # use scheduler::thread_per_core::ThreadPerCoreSched;
//! # use std::sync::atomic::{AtomicU32, Ordering};
//! # #[derive(Debug)]
//! # struct Host(u16);
//! # impl Host {
//! #     pub fn new(id: u16) -> Self { Self(id) }
//! #     pub fn id(&self) -> u16 { self.0 }
//! #     pub fn run_events(&mut self) {}
//! # }
//! // a simulation with three hosts
//! let hosts = [Host::new(0), Host::new(1), Host::new(2)];
//!
//! // a scheduler with two threads (no cpu pinning) and three hosts
//! let mut sched: ThreadPerCoreSched<Host> =
//!     ThreadPerCoreSched::new(&[None, None], hosts, false);
//!
//! // the counter is owned by this main thread with a non-static lifetime, but
//! // because of the "scoped threads" design it can be accessed by the task in
//! // the scheduler's threads
//! let counter = AtomicU32::new(0);
//!
//! // run one round of the scheduler
//! sched.scope(|s| {
//!     s.run_with_hosts(|thread_idx, hosts| {
//!         hosts.for_each(|mut host| {
//!             println!("Running host {} on thread {thread_idx}", host.id());
//!             host.run_events();
//!             counter.fetch_add(1, Ordering::Relaxed);
//!             host
//!         });
//!     });
//!
//!     // we can do other processing here in the main thread while we wait for the
//!     // above task to finish running
//!     println!("Waiting for the task to finish on all threads");
//! });
//!
//! println!("Finished processing the hosts");
//!
//! // the `counter.fetch_add(1)` was run once for each host
//! assert_eq!(counter.load(Ordering::Relaxed), 3);
//!
//! // we're done with the scheduler, so join all of its threads
//! sched.join();
//! ```
//!
//! The [`ThreadPerCoreSched`] scheduler is generally much faster and should be preferred over the
//! [`ThreadPerHostSched`] scheduler. If no one finds a situation where the `ThreadPerHostSched` is
//! faster, then it should probably be removed sometime in the future.
//!
//! It's probably good to [`box`][Box] the host since the schedulers move the host frequently, and it's
//! faster to move a pointer than the entire host object.
//!
//! Unsafe code should only be written in the thread pools. The schedulers themselves should be
//! written in only safe code using the safe interfaces provided by the thread pools. If new
//! features are needed in the scheduler, it's recommended to try to add them to the scheduler
//! itself and not modify any of the thread pools. The thread pools are complicated and have
//! delicate lifetime [sub-typing/variance][variance] handling, which is easy to break and would
//! enable the user of the scheduler to invoke undefined behaviour.
//!
//! [variance]: https://doc.rust-lang.org/nomicon/subtyping.html
//!
//! If the scheduler uses CPU pinning, the task can get the CPU its pinned to using
//! [`core_affinity`].

// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

pub mod thread_per_core;
pub mod thread_per_host;

mod logical_processor;
mod pools;
mod sync;

use std::cell::Cell;

#[cfg(doc)]
use {thread_per_core::ThreadPerCoreSched, thread_per_host::ThreadPerHostSched};

// any scheduler implementation can read/write the thread-local directly, but external modules can
// only read it using `core_affinity()`

std::thread_local! {
    /// The core affinity of the current thread, as set by the active scheduler.
    static CORE_AFFINITY: Cell<Option<u32>> = const { Cell::new(None) };
}

/// Get the core affinity of the current thread, as set by the active scheduler. Will be `None` if
/// the scheduler is not using CPU pinning, or if called from a thread not owned by the scheduler.
pub fn core_affinity() -> Option<u32> {
    CORE_AFFINITY.with(|x| x.get())
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
    /// The maximum number of threads that will ever be run in parallel. The number of threads
    /// created by the scheduler may be higher.
    pub fn parallelism(&self) -> usize {
        match self {
            Self::ThreadPerHost(sched) => sched.parallelism(),
            Self::ThreadPerCore(sched) => sched.parallelism(),
        }
    }

    /// Create a scope for any task run on the scheduler. The current thread will block at the end
    /// of the scope until the task has completed.
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

/// A scope for any task run on the scheduler.
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
    /// For each [`Host`], calls `f` with the host. The `Host` must be returned by the closure. The
    /// ownership of the `Host` is transferred in and out of the closure rather than using a mutable
    /// reference since Shadow needs to put the host in a global with `'static` lifetime (the
    /// worker).
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
