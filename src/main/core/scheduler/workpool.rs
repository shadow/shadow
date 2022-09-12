use std::marker::PhantomData;
use std::ops::{Deref, DerefMut};
use std::os::unix::thread::JoinHandleExt;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Barrier};
use std::time::Duration;

use atomic_refcell::AtomicRefCell;
use crossbeam::queue::ArrayQueue;
use crossbeam::queue::SegQueue;

pub trait TaskFn: Fn(u32, u32, u32) + Send + Sync {}
impl<T> TaskFn for T where T: Fn(u32, u32, u32) + Send + Sync {}

// TODO: implement !UnwindSafe and !RefUnwindSafe?
pub struct WorkerPool {
    parallelism: u32,
    threads: Vec<std::thread::JoinHandle<()>>,
    thread_control: Arc<ThreadControl>,
    //start_counter: LatchCounter,
    end_waiter: LatchWaiter,
}

pub struct ThreadControl {
    // the task to run during the next round
    task: AtomicRefCell<Option<Box<dyn TaskFn>>>,
    // where threads wait for the next task
    //start_barrier: CountDownLatch,
    // where threads wait after finishing the task
    //end_barrier: CountDownLatch,
    // threads which have panicked
    thread_panicked: AtomicBool,
    processors: Vec<ProcessorData>,
    threads: Vec<ThreadData>,
    threads_waiting: AtomicRefCell<Vec<ArrayQueue<usize>>>,
    threads_finished: AtomicRefCell<Vec<ArrayQueue<usize>>>,
    use_pinning: bool,
}

pub struct ProcessorData {
    //cpu_set: nix::sched::CpuSet,
    cpu_id: u32,
}

pub struct ThreadData {
    start_semaphore: Semaphore,
    tid: nix::unistd::Pid,
    processor_index: AtomicUsize,
}

impl WorkerPool {
    pub fn new(parallelism: u32, num_threads: u32, use_pinning: bool) -> Self {
        let parallelism = std::cmp::min(parallelism, num_threads);

        /*
        let start_semaphores: Vec<ArrayQueue<_>> = (0..parallelism)
            .map(|_| ArrayQueue::new(num_threads as usize))
            .collect();
        let start_semaphores_2: Vec<ArrayQueue<_>> = (0..parallelism)
            .map(|_| ArrayQueue::new(num_threads as usize))
            .collect();

        let start_semaphores = AtomicRefCell::new(start_semaphores);
        let start_semaphores_2 = AtomicRefCell::new(start_semaphores_2);
        */

        let processor_data = (0..parallelism)
            .map(|x| {
                /*
                let mut cpus = nix::sched::CpuSet::new();
                cpus.set(x as usize).unwrap();
                ProcessorData { cpu_set: cpus }
                */
                ProcessorData { cpu_id: x }
            })
            .collect();

        /*
        let start_semaphores: Vec<ArrayQueue<_>> = (0..parallelism).map(|_| ArrayQueue::new(num_threads as usize)).collect();

        for queue in start_semaphores.iter().cycle().take(num_threads as usize) {
            queue.push(Semaphore::new(0)).unwrap();
        }
        */

        /*
        let start_semaphores: Vec<Vec<_>> = (0..parallelism).map(|_| Vec::new()).collect();

        for queue in start_semaphores.iter().cycle().take(num_threads as usize) {
            queue.push(Semaphore::new(0)).unwrap();
        }
        */

        let mut threads = Vec::new();

        //let (start_counter, start_waiter) = count_down_latch();
        let (end_counter, end_waiter) = count_down_latch();

        let mut control_senders = Vec::new();
        let mut tids = Vec::new();

        /*
        for (i, (queue_id, queue)) in thread_control
            .start_semaphores
            .borrow()
            .iter()
            .enumerate()
            .cycle()
            .take(num_threads as usize)
            .enumerate()
        {
            let start_semaphore = Semaphore::new(0);
            let start_semaphore_clone = start_semaphore.clone();
            queue.push(start_semaphore).unwrap();
        */

        for i in 0..num_threads {
            let (tid_send, tid_recv) = crossbeam::channel::bounded(1);

            let (control_send, control_recv) = crossbeam::channel::bounded(1);
            control_senders.push(control_send);

            //let thread_control_clone = Arc::clone(&thread_control);
            //let start_waiter_clone = start_waiter.clone();
            //let start_semaphore_clone = start_semaphores[i as usize].clone();
            let end_counter_clone = end_counter.clone();

            let handle = std::thread::spawn(move || {
                work_loop(
                    i as u32,
                    //queue_id as u32,
                    tid_send,
                    control_recv,
                    //thread_control_clone,
                    //recv,
                    //start_waiter_clone,
                    //start_semaphore_clone,
                    end_counter_clone,
                )
            });

            tids.push(tid_recv.recv().unwrap());

            threads.push(handle);
        }

        let thread_data: Vec<ThreadData> = (0..parallelism)
            .cycle()
            .zip(tids.iter())
            .map(|(processor_index, tid)| ThreadData {
                start_semaphore: Semaphore::new(0),
                tid: *tid,
                processor_index: AtomicUsize::new(processor_index as usize),
            })
            .collect();

        let threads_waiting: Vec<ArrayQueue<_>> = (0..parallelism)
            .map(|_| ArrayQueue::new(num_threads as usize))
            .collect();
        let threads_finished: Vec<ArrayQueue<_>> = (0..parallelism)
            .map(|_| ArrayQueue::new(num_threads as usize))
            .collect();

        for (i, thread) in thread_data.iter().enumerate() {
            threads_waiting[thread.processor_index.load(Ordering::Relaxed)]
                .push(i)
                .unwrap();
        }

        let thread_control = Arc::new(ThreadControl {
            task: AtomicRefCell::new(None),
            //start_barrier: CountDownLatch::new(1, usize::try_from(num_threads).unwrap()),
            //end_barrier: CountDownLatch::new(usize::try_from(num_threads).unwrap(), 1),
            //start_semaphores,
            //start_semaphores_2,
            thread_panicked: AtomicBool::new(false),
            processors: processor_data,
            threads: thread_data,
            threads_waiting: AtomicRefCell::new(threads_waiting),
            threads_finished: AtomicRefCell::new(threads_finished),
            use_pinning,
        });

        for s in control_senders.into_iter() {
            s.send(Arc::clone(&thread_control)).unwrap();
        }

        Self {
            parallelism,
            threads,
            thread_control,
            //start_counter,
            //start_semaphores,
            //start_semaphores_2,
            end_waiter,
        }
    }

    /// The maximum number of threads that will ever be run in parallel.
    pub fn parallelism(&self) -> usize {
        self.parallelism as usize
    }

    /// The maximum number of threads that will ever be run in parallel.
    pub fn num_threads(&self) -> usize {
        self.threads.len() as usize
    }

    pub fn join(mut self) -> std::thread::Result<()> {
        assert!(self.thread_control.task.borrow().is_none());

        // if one of the threads panicked, then all threads would have exited early
        if !self.thread_control.thread_panicked.load(Ordering::Relaxed) {
            //self.thread_control.start_barrier.wait();
            /*
            for queue in self.thread_control.start_semaphores.borrow().iter() {
                while let Some(semaphore) = queue.pop() {
                    semaphore.post();
                }
            }
            */
            for thread in &self.thread_control.threads {
                thread.start_semaphore.post();
            }
        }

        for handle in self.threads.drain(..) {
            // TODO: don't return a std::thread::Result (potential unsafety?)
            // must unwrap(); it is a soundness error to receive an error
            // TODO: maybe don't unwrap and just panic instead so that we don't read the Err value
            // from the panic?
            handle.join().unwrap();
        }

        Ok(())
    }

    // TODO: unsure if the 'scope lifetime is needed on f
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a> FnOnce(TaskRunner<'a, 'scope>) + 'scope,
    ) {
        // TODO: return error or something
        assert!(!self.thread_control.thread_panicked.load(Ordering::Relaxed));

        // makes sure that the task is properly cleared even if 'f' panics
        let mut scope = WorkerScope::<'scope> {
            pool: self,
            _phantom: Default::default(),
        };

        // SAFETY: TaskRunner has a lifetime at least as large as the current function, and
        // TaskRunner is invariant so it's lifetime shouldn't be shortened within f
        let runner = TaskRunner { scope: &mut scope };

        f(runner);
    }
}

struct WorkerScope<'scope> {
    pool: &'scope mut WorkerPool,
    // when we are dropped, it's like dropping the task
    _phantom: PhantomData<Box<dyn TaskFn + 'scope>>,
}

impl<'a> std::ops::Drop for WorkerScope<'a> {
    fn drop(&mut self) {
        // if the task was set (if `TaskRunner::run` was called)
        if self.pool.thread_control.task.borrow().is_some() {
            // wait for the task to complete
            //self.pool.thread_control.end_barrier.wait();
            self.pool.end_waiter.wait();
            // clear the task
            *self.pool.thread_control.task.borrow_mut() = None;
            /*
            std::mem::swap(
                self.pool
                    .thread_control
                    .start_semaphores
                    .borrow_mut()
                    .deref_mut(),
                self.pool
                    .thread_control
                    .start_semaphores_2
                    .borrow_mut()
                    .deref_mut(),
            );
            */
            std::mem::swap(
                self.pool
                    .thread_control
                    .threads_waiting
                    .borrow_mut()
                    .deref_mut(),
                self.pool
                    .thread_control
                    .threads_finished
                    .borrow_mut()
                    .deref_mut(),
            );
        }

        // TODO: must join all threads that panicked before the scope ends
        // TODO: if a thread panicked, panic here?
        // https://docs.rs/rayon/latest/rayon/fn.scope.html#panics
    }
}

fn next_thread_index(
    current_processor_index: usize,
    processor_queues: &Vec<ArrayQueue<usize>>,
) -> Option<usize> {
    /*
    let parallelism = semaphores.len();

    for offset in 0..parallelism {
        let index = (index + offset) % parallelism;
        let from_queue = &semaphores[index];
        if let Some(next_semaphore) = from_queue.pop() {
            return Some(next_semaphore);
        }
    }
    */

    let parallelism = processor_queues.len();

    for offset in 0..parallelism {
        let processor_index = (current_processor_index + offset) % parallelism;
        let from_queue = &processor_queues[processor_index];
        if let Some(next_thread) = from_queue.pop() {
            return Some(next_thread);
        }
    }

    None
}

fn work_loop(
    thread_index: u32,
    //parallelism_index: u32,
    //thread_control: Arc<ThreadControl>,
    tid_send: crossbeam::channel::Sender<nix::unistd::Pid>,
    control_recv: crossbeam::channel::Receiver<Arc<ThreadControl>>,
    //mut start_waiter: LatchWaiter,
    //start_semaphore: Semaphore,
    mut end_counter: LatchCounter,
) {
    // this will poison the workpool when it's dropped
    struct PoisonWhenDropped<'a>(&'a ThreadControl);

    impl<'a> std::ops::Drop for PoisonWhenDropped<'a> {
        fn drop(&mut self) {
            // if we panicked, then inform other threads that we panicked and allow them to exit
            // gracefully
            self.0.thread_panicked.store(true, Ordering::Relaxed);
            //self.0.end_barrier.count_down();
        }
    }

    tid_send.send(nix::unistd::gettid()).unwrap();

    let thread_control = control_recv.recv().unwrap();
    let thread_data = &thread_control.threads[thread_index as usize];
    let start_semaphore = &thread_data.start_semaphore;

    loop {
        let thread_control = thread_control.as_ref();

        //thread_control.start_barrier.wait();
        //start_waiter.wait();
        start_semaphore.wait();
        let poison_when_dropped = PoisonWhenDropped(thread_control);
        let current_processor_index = thread_data.processor_index.load(Ordering::Relaxed);
        let cpu_id = thread_control.processors[current_processor_index].cpu_id;

        // SAFETY: scope used to make sure we drop the task before waiting
        {
            match thread_control.task.borrow().deref() {
                Some(task) => (task)(thread_index, current_processor_index as u32, cpu_id),
                None => {
                    // received the sentinel value, so forget the poison handler and exit
                    std::mem::forget(poison_when_dropped);
                    break;
                }
            };
        }

        {
            //let start_semaphores = thread_control.start_semaphores.borrow();
            let threads_waiting = thread_control.threads_waiting.borrow();

            // start next thread
            // must do this before the countdown below since the main thread will swap the two vecs
            if let Some(next_thread_index) =
                next_thread_index(current_processor_index, &threads_waiting)
            {
                let next_thread = &thread_control.threads[next_thread_index];

                // if the next thread is assigned to a different processor
                if current_processor_index != next_thread.processor_index.load(Ordering::Relaxed) {
                    // set thread's affinity
                    let mut cpus = nix::sched::CpuSet::new();
                    cpus.set(thread_control.processors[current_processor_index].cpu_id as usize)
                        .unwrap();
                    /*
                    nix::sched::sched_setaffinity(
                        next_thread.tid,
                        &thread_control.processors[current_processor_index].cpu_set,
                    )
                    .unwrap();
                    */
                    if thread_control.use_pinning {
                        nix::sched::sched_setaffinity(next_thread.tid, &cpus).unwrap();
                    }

                    // set thread's processor
                    next_thread
                        .processor_index
                        .store(current_processor_index, Ordering::Release);
                }

                // add thread to this processor's "finished" queue
                thread_control
                    .threads_finished
                    .borrow()
                    .get(current_processor_index)
                    .unwrap()
                    .push(next_thread_index)
                    .unwrap();

                // start thread
                next_thread.start_semaphore.post();
            }
        }

        // task didn't panic, so forget the poison handler and continue like normal
        std::mem::forget(poison_when_dropped);
        //thread_control.end_barrier.count_down();
        end_counter.count_down();
    }
}

pub struct TaskRunner<'a, 'scope> {
    // SAFETY: this must be a &mut so that Self is invariant over 'scope, and so that rust does not
    // shorten the lifetime 'scope
    scope: &'a mut WorkerScope<'scope>,
}

impl<'a, 'scope> TaskRunner<'a, 'scope> {
    pub fn run(self, f: impl TaskFn + 'scope) {
        let f = Box::new(f);

        // SAFETY: the closure f has a lifetime of at least the scope's lifetime 'scope
        // SAFETY: TODO
        let f = unsafe { std::mem::transmute::<Box<dyn TaskFn>, Box<dyn TaskFn + 'static>>(f) };

        *self.scope.pool.thread_control.task.borrow_mut() = Some(f);
        //self.scope.pool.thread_control.start_barrier.count_down();
        //self.scope.pool.start_counter.count_down();
        //for semaphore in self.scope.pool.start_semaphores.iter().take(self.scope.pool.parallelism as usize) {
        //for (queue_from, queue_into) in self.scope.pool.start_semaphores.iter().zip(self.scope.pool.start_semaphores_2.iter()) {

        /*
        for t in 0..self.scope.pool.parallelism {
            if let Some(semaphore) = next_semaphore(
                t as usize,
                &self.scope.pool.thread_control.start_semaphores.borrow(),
            ) {
                // TODO: set cpu affinity of thread
                // TODO: this is a little non-deterministic since the thread could push another
                // semaphore between the post() and the push()
                // TODO: set processor_index of next thread to this thread's processor index
                semaphore.post();
                self.scope
                    .pool
                    .thread_control
                    .start_semaphores_2
                    .borrow()
                    .get(t as usize)
                    .unwrap()
                    .push(semaphore);
            }
        }
        */

        let threads_waiting = self.scope.pool.thread_control.threads_waiting.borrow();
        let threads_finished = self.scope.pool.thread_control.threads_finished.borrow();

        for processor_index in 0..self.scope.pool.parallelism {
            let processor_index = processor_index as usize;
            if let Some(next_thread_index) = next_thread_index(processor_index, &threads_waiting) {
                let next_thread = &self.scope.pool.thread_control.threads[next_thread_index];

                // set thread's affinity
                let mut cpus = nix::sched::CpuSet::new();
                cpus.set(
                    self.scope.pool.thread_control.processors[processor_index].cpu_id as usize,
                )
                .unwrap();
                /*
                nix::sched::sched_setaffinity(
                    next_thread.tid,
                    &self.scope.pool.thread_control.processors[processor_index].cpu_set,
                )
                .unwrap();
                */
                if self.scope.pool.thread_control.use_pinning {
                    nix::sched::sched_setaffinity(next_thread.tid, &cpus).unwrap();
                }

                // set thread's processor
                next_thread
                    .processor_index
                    .store(processor_index, Ordering::Release);

                // add thread to this processor's "finished" queue
                threads_finished[processor_index]
                    .push(next_thread_index)
                    .unwrap();

                // start thread
                next_thread.start_semaphore.post();
            }
        }
    }
}

use std::cell::UnsafeCell;

#[derive(Clone)]
pub struct Semaphore {
    // SAFETY: the `LibcSemWrapper` must not be moved
    inner: Arc<LibcSemWrapper>,
}

impl Semaphore {
    pub fn new(val: libc::c_uint) -> Self {
        Self {
            inner: Arc::new(unsafe { LibcSemWrapper::new(val) }),
        }
    }

    pub fn wait(&self) {
        self.inner.wait()
    }

    pub fn post(&self) {
        self.inner.post()
    }
}

struct LibcSemWrapper {
    // SAFETY: the `sem_t` must not be moved
    inner: UnsafeCell<libc::sem_t>,
}

unsafe impl Send for LibcSemWrapper {}
unsafe impl Sync for LibcSemWrapper {}

impl LibcSemWrapper {
    pub unsafe fn new(val: libc::c_uint) -> Self {
        let rv = Self {
            inner: UnsafeCell::new(unsafe { std::mem::zeroed() }),
        };

        unsafe { libc::sem_init(rv.inner.get(), 0, val) };

        rv
    }

    pub fn wait(&self) {
        loop {
            let rv = unsafe { libc::sem_wait(self.inner.get()) };

            if rv == 0 {
                break;
            }

            match std::io::Error::last_os_error().kind() {
                std::io::ErrorKind::Interrupted => {}
                e => panic!("Unexpected semaphore error: {e}"),
            }
        }
    }

    pub fn post(&self) {
        loop {
            let rv = unsafe { libc::sem_post(self.inner.get()) };

            if rv == 0 {
                break;
            }

            match std::io::Error::last_os_error().kind() {
                // the man page doesn't say this can be interrupted, but may as well check
                std::io::ErrorKind::Interrupted => {}
                e => panic!("Unexpected semaphore error: {e}"),
            }
        }
    }
}

impl std::ops::Drop for LibcSemWrapper {
    fn drop(&mut self) {
        unsafe { libc::sem_destroy(self.inner.get()) };
    }
}

use std::sync::{Condvar, Mutex};

//#[derive(Debug)]
pub struct LatchCounter {
    inner: Arc<LatchInner>,
    generation: usize,
}

//#[derive(Debug)]
pub struct LatchWaiter {
    inner: Arc<LatchInner>,
    generation: usize,
}

struct LatchInner {
    lock: Mutex<LatchState>,
    cond: Condvar,
}

struct LatchState {
    generation: usize,
    counters: usize,
    waiters: usize,
    total_counters: usize,
    total_waiters: usize,
}

pub fn count_down_latch() -> (LatchCounter, LatchWaiter) {
    let inner = Arc::new(LatchInner {
        lock: Mutex::new(LatchState {
            generation: 0,
            counters: 1,
            waiters: 1,
            total_counters: 1,
            total_waiters: 1,
        }),
        cond: Condvar::new(),
    });

    let counter = LatchCounter {
        inner: Arc::clone(&inner),
        generation: 0,
    };

    let waiter = LatchWaiter {
        inner,
        generation: 0,
    };

    (counter, waiter)
}

impl LatchCounter {
    pub fn count_down(&mut self) {
        let counters;
        {
            let mut lock = self.inner.lock.lock().unwrap();

            assert_eq!(self.generation, lock.generation);
            lock.counters = lock.counters.checked_sub(1).unwrap();
            self.generation = self.generation.wrapping_add(1);

            counters = lock.counters;
        }

        if counters == 0 {
            self.inner.cond.notify_all();
        }
    }
}

impl LatchWaiter {
    pub fn wait(&mut self) {
        let lock = self.inner.lock.lock().unwrap();

        let mut lock = self
            .inner
            .cond
            .wait_while(lock, |x| self.generation != x.generation || x.counters > 0)
            .unwrap();

        lock.waiters = lock.waiters.checked_sub(1).unwrap();
        self.generation = self.generation.wrapping_add(1);

        if lock.waiters == 0 {
            lock.counters = lock.total_counters;
            lock.waiters = lock.total_waiters;
            lock.generation = lock.generation.wrapping_add(1);
        }
    }
}

impl Clone for LatchCounter {
    fn clone(&self) -> Self {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_counters = lock.total_counters.checked_add(1).unwrap();

        // if we haven't already counted down during the current generation
        if self.generation == lock.generation {
            lock.counters = lock.counters.checked_add(1).unwrap();
        }

        LatchCounter {
            inner: Arc::clone(&self.inner),
            generation: self.generation,
        }
    }
}

impl Clone for LatchWaiter {
    fn clone(&self) -> Self {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_waiters = lock.total_waiters.checked_add(1).unwrap();

        // if we haven't already waited during the current generation
        if self.generation == lock.generation {
            lock.waiters = lock.waiters.checked_add(1).unwrap();
        }

        LatchWaiter {
            inner: Arc::clone(&self.inner),
            generation: self.generation,
        }
    }
}

impl std::ops::Drop for LatchCounter {
    fn drop(&mut self) {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_counters = lock.total_counters.checked_sub(1).unwrap();

        // if we haven't already counted down during the current generation
        if self.generation == lock.generation {
            lock.counters = lock.counters.checked_sub(1).unwrap();
        }

        if lock.counters == 0 {
            self.inner.cond.notify_all();
        }
    }
}

impl std::ops::Drop for LatchWaiter {
    fn drop(&mut self) {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_waiters = lock.total_waiters.checked_sub(1).unwrap();

        // if we haven't already waited during the current generation
        if self.generation == lock.generation {
            lock.waiters = lock.waiters.checked_sub(1).unwrap();
        }

        if lock.waiters == 0 {
            lock.counters = lock.total_counters;
            lock.waiters = lock.total_waiters;
            lock.generation = lock.generation.wrapping_add(1);
        }
    }
}
