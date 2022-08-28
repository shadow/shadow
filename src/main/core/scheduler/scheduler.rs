use crate::core::scheduler::workpool::WorkerPool;
use crate::core::support::emulated_time::EmulatedTime;
use crate::core::worker;
use crate::host::host::Host;

use crossbeam::queue::ArrayQueue;

pub struct NewScheduler {
    pool: WorkerPool,
    num_threads: usize,
    thread_hosts: Vec<ArrayQueue<Host>>,
    thread_hosts_processed: Vec<ArrayQueue<Host>>,
}

impl NewScheduler {
    pub fn new(num_threads: u32, hosts: impl IntoIterator<Item = Host>) -> Self {
        let pool = WorkerPool::new(num_threads);

        let hosts: Vec<_> = hosts.into_iter().collect();

        let mut thread_hosts: Vec<_> = (0..num_threads)
            .map(|_| ArrayQueue::new(hosts.len()))
            .collect();
        let mut thread_hosts_2: Vec<_> = (0..num_threads)
            .map(|_| ArrayQueue::new(hosts.len()))
            .collect();

        // assign hosts to threads in a round-robin manner
        for (i, host) in hosts.into_iter().enumerate() {
            let thread_index = i % (num_threads as usize);
            thread_hosts[thread_index].push(host);
        }

        Self {
            pool,
            num_threads: num_threads as usize,
            thread_hosts,
            thread_hosts_processed: thread_hosts_2,
        }
    }

    //pub fn run(&mut self, f: impl Fn(&mut Host) + Send + Sync + Clone, _f2: impl Fn(&Host) -> bool + Send + Sync + Clone) {
    pub fn run(
        &mut self,
        f: impl Fn(&mut Host) + Send + Sync + Clone,
        background_f: impl FnOnce(),
    ) {
        self.pool.scope(|s| {
            s.run(|i| {
                let i = i as usize;
                for t in 0..self.num_threads {
                    while let Some(mut host) = self.thread_hosts[(i + t) % self.num_threads].pop() {
                        (f)(&mut host);
                        self.thread_hosts_processed[i].push(host);
                    }
                }
            });
            background_f();
        });

        std::mem::swap(&mut self.thread_hosts, &mut self.thread_hosts_processed);

        /*
        scope_all(self.threads.iter().map(|x| &x.thread), |scopes| {
            for (scope, hosts) in scopes.iter().zip(self.thread_hosts.iter_mut()) {
                let f = f.clone();
                if hosts.len() == 0 {
                    continue;
                }
                scope.spawn(move |_| {
                    //let a = std::time::Instant::now();
                    for host in hosts {
                        (f)(host);
                    }
                    //b += a.elapsed();
                });
            }
        });
        */
    }

    pub fn run_on_threads(&mut self, f: impl Fn(u32) + Sync + Send + Copy) {
        self.pool.scope(|s| {
            s.run(|i| (f)(i));
        });
    }

    pub fn join(self) {
        self.pool.join();

        for host_queue in self.thread_hosts.iter() {
            while let Some(host) = host_queue.pop() {
                unsafe { crate::cshadow::host_unref(host.chost()) };
            }
        }
    }

    /*
    pub fn hosts(&self) -> impl Iterator<Item = &Host> {
        self.thread_hosts.iter().flatten()
    }
    */
}

/*
pub struct NewScheduler {
    pool: rayon::ThreadPool,
    hosts: Vec<Host>,
}

impl NewScheduler {
    pub fn new(
        num_threads: u32,
        hosts: impl IntoIterator<Item = Host>,
        bootstrap_end_time: EmulatedTime,
    ) -> Self {
        let pool = rayon::ThreadPoolBuilder::new()
            .num_threads(num_threads as usize)
            .spawn_handler(|thread| {
                std::thread::spawn(move || {
                    //let mut cpu_set = nix::sched::CpuSet::new();
                    //cpu_set.set(thread.index()).unwrap();
                    //nix::sched::sched_setaffinity(nix::unistd::Pid::from_raw(0), &cpu_set).unwrap();

                    unsafe {
                        worker::Worker::new_for_this_thread(
                            worker::WorkerThreadID(thread.index() as u32),
                            bootstrap_end_time,
                        )
                    };
                    thread.run()
                });
                Ok(())
            })
            .build()
            .unwrap();

        Self {
            pool,
            hosts: hosts.into_iter().collect(),
        }
    }

    //pub fn run(&mut self, f: impl Fn(&mut Host) + Send + Sync + Clone, f2: impl Fn(&Host) -> bool + Send + Sync + Clone) {
    pub fn run(&mut self, f: impl Fn(&mut Host) + Send + Sync + Clone) {
        self.pool.in_place_scope(|scope| {
            for host in self.hosts.iter_mut() {
                //if !(f2)(host) {
                //    continue;
                //}
                let f = f.clone();
                scope.spawn(move |_| {
                    (f)(host);
                });
            }
        });
    }
}
*/

/*
pub struct NewScheduler {
    threads: Vec<ThreadInfo>,
    thread_hosts: Vec<Vec<Host>>,
}

impl NewScheduler {
    pub fn new(
        num_threads: u32,
        hosts: impl IntoIterator<Item = Host>,
        bootstrap_end_time: EmulatedTime,
    ) -> Self {
        let threads: Vec<_> = (0..num_threads)
            .map(|thread_id| ThreadInfo {
                thread: rayon::ThreadPoolBuilder::new()
                    .num_threads(1)
                    .spawn_handler(|thread| {
                        let mut cpu_set = nix::sched::CpuSet::new();
                        cpu_set.set(thread_id as usize).unwrap();
                        nix::sched::sched_setaffinity(nix::unistd::Pid::from_raw(0), &cpu_set)
                            .unwrap();

                        std::thread::spawn(move || {
                            unsafe {
                                worker::Worker::new_for_this_thread(
                                    worker::WorkerThreadID(thread.index() as u32),
                                    bootstrap_end_time,
                                )
                            };
                            thread.run()
                        });
                        Ok(())
                    })
                    .build()
                    .unwrap(),
                //hosts: Vec::new(),
            })
            .collect();

        let mut thread_hosts: Vec<_> = threads.iter().map(|_| Vec::new()).collect();

        // assign hosts to threads in a round-robin manner
        for (i, host) in hosts.into_iter().enumerate() {
            let thread_index = i % threads.len();
            thread_hosts[thread_index].push(host);
        }

        Self {
            threads,
            thread_hosts,
        }
    }

    //pub fn run(&mut self, f: impl Fn(&mut Host) + Send + Sync + Clone, _f2: impl Fn(&Host) -> bool + Send + Sync + Clone) {
    pub fn run(&mut self, f: impl Fn(&mut Host) + Send + Sync + Clone) {
        //let start = std::time::Instant::now();
        //let mut first = std::time::Duration::ZERO;
        // TODO: this scope_all() is slow
        //scope_all(self.threads.iter().map(|x| &x.thread).take(15), |scopes| {
        scope_all(self.threads.iter().map(|x| &x.thread), |scopes| {
            //first = start.elapsed();
            for (scope, hosts) in scopes.iter().zip(self.thread_hosts.iter_mut()) {
                let f = f.clone();
                if hosts.len() == 0 {
                    continue;
                }
                scope.spawn(move |_| {
                    //let a = std::time::Instant::now();
                    for host in hosts {
                        (f)(host);
                    }
                    //b += a.elapsed();
                });
            }
        });
        //let end = start.elapsed();
        //log::warn!("STEVE: {:?}/{:?}", first, start.elapsed());
        //log::warn!("STEVE: {b:?}/{end:?} ({:?})", b/end);
    }

    pub fn hosts(&self) -> impl Iterator<Item = &Host> {
        self.thread_hosts.iter().flatten()
    }
}

struct ThreadInfo {
    thread: rayon::ThreadPool,
}

#[inline]
fn scope_all<'a, 'scope>(
    pools: impl ExactSizeIterator<Item = &'a rayon::ThreadPool> + Send,
    f: impl FnOnce(Vec<&rayon::Scope<'scope>>) + Send + 'scope,
) {
    #[inline]
    fn recursive_scope<'a, 'scope>(
        mut pools: impl Iterator<Item = &'a rayon::ThreadPool> + Send,
        scopes: Vec<&rayon::Scope<'scope>>,
        f: impl FnOnce(Vec<&rayon::Scope<'scope>>) + Send + 'scope,
    ) {
        match pools.next() {
            None => return f(scopes),
            Some(pool) => {
                pool.in_place_scope(move |s| {
                    let mut scopes = scopes;
                    scopes.push(s);
                    recursive_scope(pools, scopes, f);
                });
            }
        }
    }

    let vec = Vec::with_capacity(pools.len());
    recursive_scope(pools, vec, f)
}
*/
