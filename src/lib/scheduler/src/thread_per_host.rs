use std::cell::RefCell;
use std::fmt::Debug;
use std::sync::Mutex;
use std::thread::LocalKey;

use crate::pools::bounded::{ParallelismBoundedThreadPool, TaskRunner};
use crate::CORE_AFFINITY;

pub trait Host: Debug + Send + 'static {}
impl<T> Host for T where T: Debug + Send + 'static {}

/// A host scheduler.
pub struct ThreadPerHostSched<HostType: Host> {
    /// The thread pool.
    pool: ParallelismBoundedThreadPool,
    /// Thread-local storage where a thread can store its host.
    host_storage: &'static LocalKey<RefCell<Option<HostType>>>,
}

impl<HostType: Host> ThreadPerHostSched<HostType> {
    /// A new host scheduler with logical processors that are pinned to the provided OS processors.
    /// Each logical processor is assigned many threads, and each thread is given a single host. The
    /// number of threads created will be the length of `hosts`.
    ///
    /// An empty `host_storage` for thread-local storage is required for each thread to have
    /// efficient access to its host. A panic may occur if `host_storage` is not `None`, or if it is
    /// borrowed while the scheduler is in use.
    pub fn new<T>(
        cpu_ids: &[Option<u32>],
        host_storage: &'static LocalKey<RefCell<Option<HostType>>>,
        hosts: T,
    ) -> Self
    where
        T: IntoIterator<Item = HostType>,
        <T as IntoIterator>::IntoIter: ExactSizeIterator,
    {
        let hosts = hosts.into_iter();

        let mut pool = ParallelismBoundedThreadPool::new(cpu_ids, hosts.len(), "shadow-worker");

        // for determinism, threads will take hosts from a vec rather than a queue
        let hosts: Vec<Mutex<Option<HostType>>> = hosts.map(|x| Mutex::new(Some(x))).collect();

        // have each thread take a host and store it as a thread-local
        pool.scope(|s| {
            s.run(|t| {
                host_storage.with(|x| {
                    assert!(x.borrow().is_none());
                    let host = hosts[t.thread_idx].lock().unwrap().take().unwrap();
                    *x.borrow_mut() = Some(host);
                });
            });
        });

        Self { pool, host_storage }
    }

    /// See [`crate::Scheduler::parallelism`].
    pub fn parallelism(&self) -> usize {
        self.pool.num_processors()
    }

    /// See [`crate::Scheduler::scope`].
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a> FnOnce(SchedulerScope<'a, 'scope, HostType>) + 'scope,
    ) {
        let host_storage = self.host_storage;
        self.pool.scope(move |s| {
            let sched_scope = SchedulerScope {
                runner: s,
                host_storage,
            };

            (f)(sched_scope);
        });
    }

    /// See [`crate::Scheduler::join`].
    pub fn join(mut self) {
        let hosts: Vec<Mutex<Option<HostType>>> = (0..self.pool.num_threads())
            .map(|_| Mutex::new(None))
            .collect();

        // collect all of the hosts from the threads
        self.pool.scope(|s| {
            s.run(|t| {
                self.host_storage.with(|x| {
                    let host = x.borrow_mut().take().unwrap();
                    *hosts[t.thread_idx].lock().unwrap() = Some(host);
                });
            });
        });

        self.pool.join();
    }
}

/// A wrapper around the work pool's scoped runner.
pub struct SchedulerScope<'pool, 'scope, HostType: Host> {
    /// The work pool's scoped runner.
    runner: TaskRunner<'pool, 'scope>,
    /// Thread-local storage where a thread can retrieve its host.
    host_storage: &'static LocalKey<RefCell<Option<HostType>>>,
}

impl<'pool, 'scope, HostType: Host> SchedulerScope<'pool, 'scope, HostType> {
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
    pub fn run_with_hosts(self, f: impl Fn(usize, &mut HostIter<HostType>) + Send + Sync + 'scope) {
        self.runner.run(move |task_context| {
            // update the thread-local core affinity
            if let Some(cpu_id) = task_context.cpu_id {
                CORE_AFFINITY.with(|x| *x.borrow_mut() = Some(cpu_id));
            }

            self.host_storage.with(|host| {
                let mut host = host.borrow_mut();

                let mut host_iter = HostIter { host: host.take() };

                f(task_context.thread_idx, &mut host_iter);

                host.replace(host_iter.host.take().unwrap());
            });
        });
    }

    /// See [`crate::core::scheduler::SchedulerScope::run_with_data`].
    pub fn run_with_data<T>(
        self,
        data: &'scope [T],
        f: impl Fn(usize, &mut HostIter<HostType>, &T) + Send + Sync + 'scope,
    ) where
        T: Sync,
    {
        self.runner.run(move |task_context| {
            // update the thread-local core affinity
            if let Some(cpu_id) = task_context.cpu_id {
                CORE_AFFINITY.with(|x| *x.borrow_mut() = Some(cpu_id));
            }

            let this_elem = &data[task_context.processor_idx];

            self.host_storage.with(|host| {
                let mut host = host.borrow_mut();

                let mut host_iter = HostIter { host: host.take() };

                f(task_context.thread_idx, &mut host_iter, this_elem);

                host.replace(host_iter.host.unwrap());
            });
        });
    }
}

/// Supports iterating over all hosts assigned to this thread. For this thread-per-host scheduler,
/// there will only ever be one host per thread.
pub struct HostIter<HostType: Host> {
    host: Option<HostType>,
}

impl<HostType: Host> HostIter<HostType> {
    /// See [`crate::core::scheduler::HostIter::for_each`].
    pub fn for_each<F>(&mut self, mut f: F)
    where
        F: FnMut(HostType) -> HostType,
    {
        let host = self.host.take().unwrap();
        self.host.replace(f(host));
    }
}

#[cfg(any(test, doctest))]
mod tests {
    use std::cell::RefCell;
    use std::sync::atomic::{AtomicU32, Ordering};

    use super::*;

    #[derive(Debug)]
    struct TestHost {}

    std::thread_local! {
        static SCHED_HOST_STORAGE: RefCell<Option<TestHost>> = const { RefCell::new(None) };
    }

    #[test]
    fn test_parallelism() {
        let hosts = [(); 5].map(|_| TestHost {});
        let sched: ThreadPerHostSched<TestHost> =
            ThreadPerHostSched::new(&[None, None], &SCHED_HOST_STORAGE, hosts);

        assert_eq!(sched.parallelism(), 2);

        sched.join();
    }

    #[test]
    fn test_no_join() {
        let hosts = [(); 5].map(|_| TestHost {});
        let _sched: ThreadPerHostSched<TestHost> =
            ThreadPerHostSched::new(&[None, None], &SCHED_HOST_STORAGE, hosts);
    }

    #[test]
    #[should_panic]
    fn test_panic() {
        let hosts = [(); 5].map(|_| TestHost {});
        let mut sched: ThreadPerHostSched<TestHost> =
            ThreadPerHostSched::new(&[None, None], &SCHED_HOST_STORAGE, hosts);

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
        let mut sched: ThreadPerHostSched<TestHost> =
            ThreadPerHostSched::new(&[None, None], &SCHED_HOST_STORAGE, hosts);

        let counter = AtomicU32::new(0);

        for _ in 0..3 {
            sched.scope(|s| {
                s.run(|_| {
                    counter.fetch_add(1, Ordering::SeqCst);
                });
            });
        }

        assert_eq!(counter.load(Ordering::SeqCst), 5 * 3);

        sched.join();
    }

    #[test]
    fn test_run_with_hosts() {
        let hosts = [(); 5].map(|_| TestHost {});
        let mut sched: ThreadPerHostSched<TestHost> =
            ThreadPerHostSched::new(&[None, None], &SCHED_HOST_STORAGE, hosts);

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
        let mut sched: ThreadPerHostSched<TestHost> =
            ThreadPerHostSched::new(&[None, None], &SCHED_HOST_STORAGE, hosts);

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
