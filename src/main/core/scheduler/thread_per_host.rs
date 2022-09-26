use std::cell::RefCell;
use std::marker::PhantomData;
use std::sync::Mutex;

use crate::core::scheduler::pools::bounded::{TaskRunner, WorkPool};
use crate::host::host::Host;

use super::CORE_AFFINITY;

std::thread_local! {
    /// The host that belongs to this thread.
    static THREAD_HOST: RefCell<Option<Host>> = RefCell::new(None);
}

/// A host scheduler.
pub struct Scheduler {
    pool: WorkPool,
}

impl Scheduler {
    /// A new host scheduler with logical processors that are pinned to the provided OS processors.
    /// Each logical processor is assigned many threads, and each thread is given a single host.
    pub fn new<T>(cpu_ids: &[Option<u32>], hosts: T) -> Self
    where
        T: IntoIterator<Item = Host>,
        <T as IntoIterator>::IntoIter: ExactSizeIterator,
    {
        let hosts = hosts.into_iter();

        let mut pool = WorkPool::new(cpu_ids, hosts.len(), "shadow-worker");

        // for determinism, threads will take hosts from a vec rather than a queue
        let hosts: Vec<Mutex<Option<Host>>> = hosts.map(|x| Mutex::new(Some(x))).collect();

        // have each thread take a host and store it as a thread-local
        pool.scope(|s| {
            s.run(|t| {
                THREAD_HOST.with(|x| {
                    let host = hosts[t.thread_idx].lock().unwrap().take().unwrap();
                    *x.borrow_mut() = Some(host);
                });
            });
        });

        Self { pool }
    }

    /// The maximum number of threads that will ever be run in parallel.
    pub fn parallelism(&self) -> usize {
        self.pool.num_processors()
    }

    /// A scope for any task run on the scheduler. The current thread will block at the end of the
    /// scope until the task has completed.
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a, 'b> FnOnce(SchedScope<'a, 'b, 'scope>) + 'scope,
    ) {
        self.pool.scope(move |s| {
            let sched_scope = SchedScope {
                runner: s,
                marker: Default::default(),
            };

            (f)(sched_scope);
        });
    }

    /// Join all threads started by the scheduler.
    pub fn join(mut self) {
        let hosts: Vec<Mutex<Option<Host>>> = (0..self.pool.num_threads())
            .map(|_| Mutex::new(None))
            .collect();

        // collect all of the hosts from the threads
        self.pool.scope(|s| {
            s.run(|t| {
                THREAD_HOST.with(|x| {
                    let host = x.borrow_mut().take().unwrap();
                    *hosts[t.thread_idx].lock().unwrap() = Some(host);
                });
            });
        });

        // need to unref the host from the main thread so that the global allocation counter will be
        // correctly updated
        for host in hosts {
            if let Some(host) = host.lock().unwrap().take() {
                unsafe { crate::cshadow::host_unref(host.chost()) };
            }
        }

        self.pool.join();
    }
}

/// A wrapper around the work pool's scoped runner.
pub struct SchedScope<'sched, 'pool, 'scope>
where
    'sched: 'scope,
{
    runner: TaskRunner<'pool, 'scope>,
    // TODO: other schedulers (such as a thread-per-core scheduler) may need this, so leaving this
    // here for now in case we need to provide an interface that has a 'sched lifetime
    marker: PhantomData<&'sched Host>,
}

impl<'sched, 'pool, 'scope> SchedScope<'sched, 'pool, 'scope> {
    /// Run the closure on all threads. The closure is given an index of the currently running
    /// thread.
    pub fn run(self, f: impl Fn(usize) + Sync + Send + 'scope) {
        self.runner.run(move |task_context| {
            // update the thread-local core affinity
            if let Some(cpu_id) = task_context.cpu_id {
                CORE_AFFINITY.with(|x| *x.borrow_mut() = Some(cpu_id));
            }

            (f)(task_context.thread_idx)
        });
    }

    /// Run the closure on all threads. The closure is given an index of the currently running
    /// thread and a host iterator.
    ///
    /// The closure must iterate over the provided `HostIter` to completion (until `next()` returns
    /// `None`), otherwise this may panic. The host iterator is not a real [`std::iter::Iterator`],
    /// but rather a fake iterator that behaves like a streaming iterator.
    pub fn run_with_hosts(self, f: impl Fn(usize, &mut HostIter) + Send + Sync + 'scope) {
        self.runner.run(move |task_context| {
            // update the thread-local core affinity
            if let Some(cpu_id) = task_context.cpu_id {
                CORE_AFFINITY.with(|x| *x.borrow_mut() = Some(cpu_id));
            }

            THREAD_HOST.with(|host| {
                let mut host = host.borrow_mut();

                let mut host_iter = HostIter {
                    host: Some(host.as_mut().unwrap()),
                };

                f(task_context.thread_idx, &mut host_iter);
            });
        });
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
        self.runner.run(move |task_context| {
            // update the thread-local core affinity
            if let Some(cpu_id) = task_context.cpu_id {
                CORE_AFFINITY.with(|x| *x.borrow_mut() = Some(cpu_id));
            }

            let this_elem = &data[task_context.processor_idx];

            THREAD_HOST.with(|host| {
                let mut host = host.borrow_mut();

                let mut host_iter = HostIter {
                    host: Some(host.as_mut().unwrap()),
                };

                f(task_context.thread_idx, &mut host_iter, this_elem);
            });
        });
    }
}

/// Supports iterating over all hosts assigned to this thread. For this thread-per-host scheduler,
/// there will only ever be one host per thread.
pub struct HostIter<'a> {
    host: Option<&'a mut Host>,
}

impl<'a> HostIter<'a> {
    /// Get the next host.
    pub fn next(&mut self) -> Option<&mut Host> {
        self.host.take()
    }
}
