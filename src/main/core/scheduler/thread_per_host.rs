use std::cell::RefCell;
use std::sync::Mutex;

use crate::core::scheduler::pools::bounded::{ParallelismBoundedThreadPool, TaskRunner};
use crate::host::host::Host;

use super::CORE_AFFINITY;

std::thread_local! {
    /// The host that belongs to this thread.
    static THREAD_HOST: RefCell<Option<Host>> = RefCell::new(None);
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
        T: IntoIterator<Item = Host>,
        <T as IntoIterator>::IntoIter: ExactSizeIterator,
    {
        let hosts = hosts.into_iter();

        let mut pool = ParallelismBoundedThreadPool::new(cpu_ids, hosts.len(), "shadow-worker");

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
                    host: host.take(),
                    returned_host: None,
                };

                f(task_context.thread_idx, &mut host_iter);

                host.replace(host_iter.returned_host.take().unwrap());
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
                    host: host.take(),
                    returned_host: None,
                };

                f(task_context.thread_idx, &mut host_iter, this_elem);

                host.replace(host_iter.returned_host.unwrap());
            });
        });
    }
}

/// Supports iterating over all hosts assigned to this thread. For this thread-per-host scheduler,
/// there will only ever be one host per thread.
pub struct HostIter {
    host: Option<Host>,
    returned_host: Option<Host>,
}

impl HostIter {
    /// See [`crate::core::scheduler::HostIter::next`].
    pub fn next(&mut self, prev: Option<Host>) -> Option<Host> {
        if let Some(prev) = prev {
            assert!(self.returned_host.replace(prev).is_none())
        }
        self.host.take()
    }
}
