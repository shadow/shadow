//! A thread-per-core host scheduler.

// unsafe code should be isolated to the thread pool
#![forbid(unsafe_code)]

use std::fmt::Debug;

use crossbeam::queue::ArrayQueue;

use crate::pools::unbounded::{TaskRunner, UnboundedThreadPool};
use crate::CORE_AFFINITY;

pub trait Host: Debug + Send {}
impl<T> Host for T where T: Debug + Send {}

/// A host scheduler.
pub struct ThreadPerCoreSched<HostType: Host> {
    pool: UnboundedThreadPool,
    num_threads: usize,
    thread_hosts: Vec<ArrayQueue<HostType>>,
    thread_hosts_processed: Vec<ArrayQueue<HostType>>,
    hosts_need_swap: bool,
}

impl<HostType: Host> ThreadPerCoreSched<HostType> {
    /// A new host scheduler with threads that are pinned to the provided OS processors. Each thread
    /// is assigned many hosts, and threads may steal hosts from other threads. The number of
    /// threads created will be the length of `cpu_ids`.
    pub fn new<T>(cpu_ids: &[Option<u32>], hosts: T, yield_spin: bool) -> Self
    where
        T: IntoIterator<Item = HostType, IntoIter: ExactSizeIterator>,
    {
        let hosts = hosts.into_iter();

        let num_threads = cpu_ids.len();
        let mut pool = UnboundedThreadPool::new(num_threads, "shadow-worker", yield_spin);

        // set the affinity of each thread
        pool.scope(|s| {
            s.run(|i| {
                let cpu_id = cpu_ids[i];

                if let Some(cpu_id) = cpu_id {
                    let mut cpus = nix::sched::CpuSet::new();
                    cpus.set(cpu_id as usize).unwrap();
                    nix::sched::sched_setaffinity(nix::unistd::Pid::from_raw(0), &cpus).unwrap();

                    // update the thread-local core affinity
                    CORE_AFFINITY.with(|x| x.set(Some(cpu_id)));
                }
            });
        });

        // each thread gets two fixed-sized queues with enough capacity to store every host
        let thread_hosts: Vec<_> = (0..num_threads)
            .map(|_| ArrayQueue::new(hosts.len()))
            .collect();
        let thread_hosts_2: Vec<_> = (0..num_threads)
            .map(|_| ArrayQueue::new(hosts.len()))
            .collect();

        // assign hosts to threads in a round-robin manner
        for (thread_queue, host) in thread_hosts.iter().cycle().zip(hosts) {
            thread_queue.push(host).unwrap();
        }

        Self {
            pool,
            num_threads,
            thread_hosts,
            thread_hosts_processed: thread_hosts_2,
            hosts_need_swap: false,
        }
    }

    /// See [`crate::Scheduler::parallelism`].
    pub fn parallelism(&self) -> usize {
        self.num_threads
    }

    /// See [`crate::Scheduler::scope`].
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a, 'b> FnOnce(SchedulerScope<'a, 'b, 'scope, HostType>) + 'scope,
    ) {
        // we can't swap after the below `pool.scope()` due to lifetime restrictions, so we need to
        // do it before instead
        if self.hosts_need_swap {
            debug_assert!(self.thread_hosts.iter().all(|queue| queue.is_empty()));

            std::mem::swap(&mut self.thread_hosts, &mut self.thread_hosts_processed);
            self.hosts_need_swap = false;
        }

        // data/references that we'll pass to the scope
        let thread_hosts = &self.thread_hosts;
        let thread_hosts_processed = &self.thread_hosts_processed;
        let hosts_need_swap = &mut self.hosts_need_swap;

        // we cannot access `self` after calling `pool.scope()` since `SchedulerScope` has a
        // lifetime of `'scope` (which at minimum spans the entire current function)

        self.pool.scope(move |s| {
            let sched_scope = SchedulerScope {
                thread_hosts,
                thread_hosts_processed,
                hosts_need_swap,
                runner: s,
            };

            (f)(sched_scope);
        });
    }

    /// See [`crate::Scheduler::join`].
    pub fn join(self) {
        self.pool.join();
    }
}

/// A wrapper around the work pool's scoped runner.
pub struct SchedulerScope<'sched, 'pool, 'scope, HostType: Host>
where
    'sched: 'scope,
{
    thread_hosts: &'sched Vec<ArrayQueue<HostType>>,
    thread_hosts_processed: &'sched Vec<ArrayQueue<HostType>>,
    hosts_need_swap: &'sched mut bool,
    runner: TaskRunner<'pool, 'scope>,
}

impl<'sched, 'pool, 'scope, HostType: Host> SchedulerScope<'sched, 'pool, 'scope, HostType> {
    /// See [`crate::SchedulerScope::run`].
    pub fn run(self, f: impl Fn(usize) + Sync + Send + 'scope) {
        self.runner.run(f);
    }

    /// See [`crate::SchedulerScope::run_with_hosts`].
    pub fn run_with_hosts(
        self,
        f: impl Fn(usize, &mut HostIter<'_, HostType>) + Send + Sync + 'scope,
    ) {
        self.runner.run(move |i| {
            let mut host_iter = HostIter {
                thread_hosts_from: self.thread_hosts,
                thread_hosts_to: &self.thread_hosts_processed[i],
                this_thread_index: i,
            };

            f(i, &mut host_iter);
        });

        *self.hosts_need_swap = true;
    }

    /// See [`crate::SchedulerScope::run_with_data`].
    pub fn run_with_data<T>(
        self,
        data: &'scope [T],
        f: impl Fn(usize, &mut HostIter<'_, HostType>, &T) + Send + Sync + 'scope,
    ) where
        T: Sync,
    {
        self.runner.run(move |i| {
            let this_elem = &data[i];

            let mut host_iter = HostIter {
                thread_hosts_from: self.thread_hosts,
                thread_hosts_to: &self.thread_hosts_processed[i],
                this_thread_index: i,
            };

            f(i, &mut host_iter, this_elem);
        });

        *self.hosts_need_swap = true;
    }
}

/// Supports iterating over all hosts assigned to this thread. For this thread-per-core scheduler,
/// the iterator may steal hosts from other threads.
pub struct HostIter<'a, HostType: Host> {
    /// Queues to take hosts from.
    thread_hosts_from: &'a [ArrayQueue<HostType>],
    /// The queue to add hosts to when done with them.
    thread_hosts_to: &'a ArrayQueue<HostType>,
    /// The index of this thread. This is the first queue of `thread_hosts_from` that we take hosts
    /// from.
    this_thread_index: usize,
}

impl<'a, HostType: Host> HostIter<'a, HostType> {
    /// See [`crate::HostIter::for_each`].
    pub fn for_each<F>(&mut self, mut f: F)
    where
        F: FnMut(HostType) -> HostType,
    {
        for from_queue in self
            .thread_hosts_from
            .iter()
            .cycle()
            // start from the current thread index
            .skip(self.this_thread_index)
            .take(self.thread_hosts_from.len())
        {
            while let Some(host) = from_queue.pop() {
                self.thread_hosts_to.push(f(host)).unwrap();
            }
        }
    }
}

#[cfg(any(test, doctest))]
mod tests {
    use std::sync::atomic::{AtomicU32, Ordering};

    use super::*;

    #[derive(Debug)]
    struct TestHost {}

    #[test]
    fn test_parallelism() {
        let hosts = [(); 5].map(|_| TestHost {});
        let sched: ThreadPerCoreSched<TestHost> =
            ThreadPerCoreSched::new(&[None, None], hosts, false);

        assert_eq!(sched.parallelism(), 2);

        sched.join();
    }

    #[test]
    fn test_no_join() {
        let hosts = [(); 5].map(|_| TestHost {});
        let _sched: ThreadPerCoreSched<TestHost> =
            ThreadPerCoreSched::new(&[None, None], hosts, false);
    }

    #[test]
    #[should_panic]
    fn test_panic() {
        let hosts = [(); 5].map(|_| TestHost {});
        let mut sched: ThreadPerCoreSched<TestHost> =
            ThreadPerCoreSched::new(&[None, None], hosts, false);

        sched.scope(|s| {
            s.run(|x| {
                if x == 1 {
                    panic!();
                }
            });
        });
    }

    #[test]
    fn test_run() {
        let hosts = [(); 5].map(|_| TestHost {});
        let mut sched: ThreadPerCoreSched<TestHost> =
            ThreadPerCoreSched::new(&[None, None], hosts, false);

        let counter = AtomicU32::new(0);

        for _ in 0..3 {
            sched.scope(|s| {
                s.run(|_| {
                    counter.fetch_add(1, Ordering::SeqCst);
                });
            });
        }

        assert_eq!(counter.load(Ordering::SeqCst), 2 * 3);

        sched.join();
    }

    #[test]
    fn test_run_with_hosts() {
        let hosts = [(); 5].map(|_| TestHost {});
        let mut sched: ThreadPerCoreSched<TestHost> =
            ThreadPerCoreSched::new(&[None, None], hosts, false);

        let counter = AtomicU32::new(0);

        for _ in 0..3 {
            sched.scope(|s| {
                s.run_with_hosts(|_, hosts| {
                    hosts.for_each(|host| {
                        counter.fetch_add(1, Ordering::SeqCst);
                        host
                    });
                });
            });
        }

        assert_eq!(counter.load(Ordering::SeqCst), 5 * 3);

        sched.join();
    }

    #[test]
    fn test_run_with_data() {
        let hosts = [(); 5].map(|_| TestHost {});
        let mut sched: ThreadPerCoreSched<TestHost> =
            ThreadPerCoreSched::new(&[None, None], hosts, false);

        let data = vec![0u32; sched.parallelism()];
        let data: Vec<_> = data.into_iter().map(std::sync::Mutex::new).collect();

        for _ in 0..3 {
            sched.scope(|s| {
                s.run_with_data(&data, |_, hosts, elem| {
                    let mut elem = elem.lock().unwrap();
                    hosts.for_each(|host| {
                        *elem += 1;
                        host
                    });
                });
            });
        }

        let sum: u32 = data.into_iter().map(|x| x.into_inner().unwrap()).sum();
        assert_eq!(sum, 5 * 3);

        sched.join();
    }
}
