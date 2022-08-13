use crate::core::support::emulated_time::EmulatedTime;
use crate::core::worker;
use crate::host::host::Host;

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
            .map(|_| ThreadInfo {
                thread: rayon::ThreadPoolBuilder::new()
                    .num_threads(1)
                    .spawn_handler(|thread| {
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

    pub fn run(&mut self, f: impl Fn(&mut Host) + Send + Sync + Clone) {
        scope_all(self.threads.iter().map(|x| &x.thread), |scopes| {
            for (scope, hosts) in scopes.iter().zip(self.thread_hosts.iter_mut()) {
                let f = f.clone();
                scope.spawn(move |_| {
                    for host in hosts {
                        (f)(host);
                    }
                });
            }
        });
    }
}

struct ThreadInfo {
    thread: rayon::ThreadPool,
}

fn scope_all<'a, 'scope>(
    pools: impl ExactSizeIterator<Item = &'a rayon::ThreadPool> + Send,
    f: impl FnOnce(Vec<&rayon::Scope<'scope>>) + Send + 'scope,
) {
    fn recursive_scope<'a, 'scope>(
        mut pools: impl ExactSizeIterator<Item = &'a rayon::ThreadPool> + Send,
        scopes: Vec<&rayon::Scope<'scope>>,
        f: impl FnOnce(Vec<&rayon::Scope<'scope>>) + Send + 'scope,
    ) {
        match pools.next() {
            None => return f(scopes),
            Some(pool) => {
                pool.scope(move |s| {
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
