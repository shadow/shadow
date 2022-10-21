use crossbeam::queue::ArrayQueue;

use crate::core::scheduler::pools::unbounded::{TaskRunner, UnboundedThreadPool};
use crate::host::host::HostRef;

use super::CORE_AFFINITY;

/// A host scheduler.
pub struct ThreadPerCoreSched {
    pool: UnboundedThreadPool,
    num_threads: usize,
    thread_hosts: Vec<ArrayQueue<HostRef>>,
    thread_hosts_processed: Vec<ArrayQueue<HostRef>>,
    hosts_need_swap: bool,
}

impl ThreadPerCoreSched {
    /// A new host scheduler with threads that are pinned to the provided OS processors. Each thread
    /// is assigned many hosts, and threads may steal hosts from other threads.
    pub fn new<T>(cpu_ids: &[Option<u32>], hosts: T) -> Self
    where
        T: IntoIterator<Item = HostRef>,
        <T as IntoIterator>::IntoIter: ExactSizeIterator,
    {
        let hosts = hosts.into_iter();

        let num_threads = cpu_ids.len();
        let mut pool = UnboundedThreadPool::new(num_threads, "shadow-worker");

        // set the affinity of each thread
        pool.scope(|s| {
            s.run(|i| {
                let cpu_id = cpu_ids[i];

                if let Some(cpu_id) = cpu_id {
                    let mut cpus = nix::sched::CpuSet::new();
                    cpus.set(cpu_id as usize).unwrap();
                    nix::sched::sched_setaffinity(nix::unistd::Pid::from_raw(0), &cpus).unwrap();

                    // update the thread-local core affinity
                    CORE_AFFINITY.with(|x| *x.borrow_mut() = Some(cpu_id));
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

    /// See [`crate::core::scheduler::Scheduler::parallelism`].
    pub fn parallelism(&self) -> usize {
        self.num_threads
    }

    /// See [`crate::core::scheduler::Scheduler::scope`].
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a, 'b> FnOnce(SchedulerScope<'a, 'b, 'scope>) + 'scope,
    ) {
        // we can't swap after the below `pool.scope()` due to lifetime restrictions, so we need to
        // do it before instead
        if self.hosts_need_swap {
            debug_assert!(self.thread_hosts.iter().all(|queue| queue.len() == 0));

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

    /// See [`crate::core::scheduler::Scheduler::join`].
    pub fn join(self) {
        self.pool.join();

        // when the host is in rust we won't need to do this
        for host_queue in self.thread_hosts.iter() {
            while let Some(host) = host_queue.pop() {
                unsafe { crate::cshadow::host_unref(host.chost()) };
            }
        }
    }
}

/// A wrapper around the work pool's scoped runner.
pub struct SchedulerScope<'sched, 'pool, 'scope>
where
    'sched: 'scope,
{
    thread_hosts: &'sched Vec<ArrayQueue<HostRef>>,
    thread_hosts_processed: &'sched Vec<ArrayQueue<HostRef>>,
    hosts_need_swap: &'sched mut bool,
    runner: TaskRunner<'pool, 'scope>,
}

impl<'sched, 'pool, 'scope> SchedulerScope<'sched, 'pool, 'scope> {
    /// See [`crate::core::scheduler::SchedulerScope::run`].
    pub fn run(self, f: impl Fn(usize) + Sync + Send + 'scope) {
        self.runner.run(f);
    }

    /// See [`crate::core::scheduler::SchedulerScope::run_with_hosts`].
    pub fn run_with_hosts(self, f: impl Fn(usize, &mut HostIter) + Send + Sync + 'scope) {
        self.runner.run(move |i| {
            let mut host_iter = HostIter {
                thread_hosts_from: &self.thread_hosts,
                thread_hosts_to: &self.thread_hosts_processed[i],
                this_thread_index: i,
                thread_index_iter_offset: 0,
                current_host: None,
            };

            f(i, &mut host_iter);

            assert!(host_iter.current_host.is_none());
            assert!(host_iter.next().is_none());
        });

        *self.hosts_need_swap = true;
    }

    /// See [`crate::core::scheduler::SchedulerScope::run_with_data`].
    pub fn run_with_data<T>(
        self,
        data: &'scope [T],
        f: impl Fn(usize, &mut HostIter, &T) + Send + Sync + 'scope,
    ) where
        T: Sync,
    {
        self.runner.run(move |i| {
            let this_elem = &data[i];

            let mut host_iter = HostIter {
                thread_hosts_from: &self.thread_hosts,
                thread_hosts_to: &self.thread_hosts_processed[i],
                this_thread_index: i,
                thread_index_iter_offset: 0,
                current_host: None,
            };

            f(i, &mut host_iter, this_elem);

            assert!(host_iter.current_host.is_none());
            assert!(host_iter.next().is_none());
        });

        *self.hosts_need_swap = true;
    }
}

/// Supports iterating over all hosts assigned to this thread. For this thread-per-core scheduler,
/// the iterator may steal hosts from other threads.
pub struct HostIter<'a> {
    /// Queues to take hosts from.
    thread_hosts_from: &'a [ArrayQueue<HostRef>],
    /// The queue to add hosts to when done with them.
    thread_hosts_to: &'a ArrayQueue<HostRef>,
    /// The index of this thread. This is the first queue of `thread_hosts_from` that we take hosts
    /// from.
    this_thread_index: usize,
    /// The thread offset of our iterator; stored so that we can resume where we left off.
    thread_index_iter_offset: usize,
    /// The host that was last returned from `next()`.
    current_host: Option<HostRef>,
}

impl<'a> HostIter<'a> {
    /// See [`crate::core::scheduler::HostIter::next`].
    pub fn next(&mut self) -> Option<&mut HostRef> {
        self.return_current_host();

        // a generator would be nice here...
        for from_queue in self
            .thread_hosts_from
            .iter()
            .cycle()
            // start from the current thread index
            .skip(self.this_thread_index)
            .take(self.thread_hosts_from.len())
            // skip to where we last left off
            .skip(self.thread_index_iter_offset)
        {
            if let Some(host) = from_queue.pop() {
                // yield the host, but keep ownership so that we can add it back to the proper
                // queue later
                self.current_host = Some(host);
                return self.current_host.as_mut();
            }

            // no hosts remaining in this queue, so keep our persistent offset up-to-date
            self.thread_index_iter_offset += 1;
        }

        None
    }

    /// Returns the currently stored host back to a queue.
    fn return_current_host(&mut self) {
        if let Some(current_host) = self.current_host.take() {
            self.thread_hosts_to.push(current_host).unwrap();
        }
    }
}

impl<'a> std::ops::Drop for HostIter<'a> {
    fn drop(&mut self) {
        // make sure we don't own and drop a host
        self.return_current_host();
    }
}
