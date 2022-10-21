use std::cell::RefCell;
use std::sync::Mutex;

use crate::core::scheduler::pools::bounded::{ParallelismBoundedThreadPool, TaskRunner};
use crate::host::host::HostRef;

use super::CORE_AFFINITY;

std::thread_local! {
    /// The host that belongs to this thread.
    static THREAD_HOST: RefCell<Option<HostRef>> = RefCell::new(None);
}

/// A host scheduler.
pub struct ThreadPerHostSched {
    pool: ParallelismBoundedThreadPool,
}

impl ThreadPerHostSched {
    /// A new host scheduler with logical processors that are pinned to the provided OS processors.
    /// Each logical processor is assigned many threads, and each thread is given a single host.
    pub fn new<T>(cpu_ids: &[Option<u32>], hosts: T) -> Self
    where
        T: IntoIterator<Item = HostRef>,
        <T as IntoIterator>::IntoIter: ExactSizeIterator,
    {
        let hosts = hosts.into_iter();

        let mut pool = ParallelismBoundedThreadPool::new(cpu_ids, hosts.len(), "shadow-worker");

        // for determinism, threads will take hosts from a vec rather than a queue
        let hosts: Vec<Mutex<Option<HostRef>>> = hosts.map(|x| Mutex::new(Some(x))).collect();

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

    /// See [`crate::core::scheduler::Scheduler::parallelism`].
    pub fn parallelism(&self) -> usize {
        self.pool.num_processors()
    }

    /// See [`crate::core::scheduler::Scheduler::scope`].
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a> FnOnce(SchedulerScope<'a, 'scope>) + 'scope,
    ) {
        self.pool.scope(move |s| {
            let sched_scope = SchedulerScope { runner: s };

            (f)(sched_scope);
        });
    }

    /// See [`crate::core::scheduler::Scheduler::join`].
    pub fn join(mut self) {
        let hosts: Vec<Mutex<Option<HostRef>>> = (0..self.pool.num_threads())
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
pub struct SchedulerScope<'pool, 'scope> {
    runner: TaskRunner<'pool, 'scope>,
}

impl<'pool, 'scope> SchedulerScope<'pool, 'scope> {
    /// See [`crate::core::scheduler::SchedulerScope::run`].
    pub fn run(self, f: impl Fn(usize) + Sync + Send + 'scope) {
        self.runner.run(move |task_context| {
            // update the thread-local core affinity
            if let Some(cpu_id) = task_context.cpu_id {
                CORE_AFFINITY.with(|x| *x.borrow_mut() = Some(cpu_id));
            }

            (f)(task_context.thread_idx)
        });
    }

    /// See [`crate::core::scheduler::SchedulerScope::run_with_hosts`].
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

    /// See [`crate::core::scheduler::SchedulerScope::run_with_data`].
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
    host: Option<&'a mut HostRef>,
}

impl<'a> HostIter<'a> {
    /// See [`crate::core::scheduler::HostIter::next`].
    pub fn next(&mut self) -> Option<&mut HostRef> {
        self.host.take()
    }
}
