use std::marker::PhantomData;
use std::ops::Deref;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

pub trait TaskFn: Fn(u32) + Send + Sync {}
impl<T> TaskFn for T where T: Fn(u32) + Send + Sync {}

// TODO: implement !UnwindSafe and !RefUnwindSafe?
pub struct WorkerPool {
    threads: Vec<std::thread::JoinHandle<()>>,
    thread_control: Arc<ThreadControl>,
    //start_counter: LatchCounter,
    start_semaphores: Vec<Semaphore>,
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
}

impl WorkerPool {
    pub fn new(num_threads: u32) -> Self {
        let mut threads = Vec::new();

        let thread_control = Arc::new(ThreadControl {
            task: AtomicRefCell::new(None),
            //start_barrier: CountDownLatch::new(1, usize::try_from(num_threads).unwrap()),
            //end_barrier: CountDownLatch::new(usize::try_from(num_threads).unwrap(), 1),
            thread_panicked: AtomicBool::new(false),
        });

        let start_semaphores: Vec<_> = (0..num_threads).map(|_| Semaphore::new(0)).collect();

        //let (start_counter, start_waiter) = count_down_latch();
        let (end_counter, end_waiter) = count_down_latch();

        for i in 0..num_threads {
            let thread_control_clone = Arc::clone(&thread_control);
            //let start_waiter_clone = start_waiter.clone();
            let start_semaphore_clone = start_semaphores[i as usize].clone();
            let end_counter_clone = end_counter.clone();

            let handle = std::thread::spawn(move || {
                work_loop(
                    i,
                    thread_control_clone,
                    //start_waiter_clone,
                    start_semaphore_clone,
                    end_counter_clone,
                )
            });

            threads.push(handle);
        }

        Self {
            threads,
            thread_control,
            //start_counter,
            start_semaphores,
            end_waiter,
        }
    }

    pub fn join(mut self) -> std::thread::Result<()> {
        assert!(self.thread_control.task.borrow().is_none());

        // if one of the threads panicked, then all threads would have exited early
        if !self.thread_control.thread_panicked.load(Ordering::Relaxed) {
            //self.thread_control.start_barrier.wait();
            //self.start_counter.count_down();
            for semaphore in &self.start_semaphores {
                semaphore.post();
            }
            //todo!();
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
        }

        // TODO: must join all threads that panicked before the scope ends
        // TODO: if a thread panicked, panic here?
        // https://docs.rs/rayon/latest/rayon/fn.scope.html#panics
    }
}

fn work_loop(
    thread_index: u32,
    thread_control: Arc<ThreadControl>,
    //mut start_waiter: LatchWaiter,
    start_semaphore: Semaphore,
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

    loop {
        let thread_control = thread_control.as_ref();

        //thread_control.start_barrier.wait();
        //start_waiter.wait();
        start_semaphore.wait();
        let poison_when_dropped = PoisonWhenDropped(thread_control);

        // SAFETY: scope used to make sure we drop the task before waiting
        {
            match thread_control.task.borrow().deref() {
                Some(task) => (task)(thread_index),
                None => {
                    // received the sentinel value, so forget the poison handler and exit
                    std::mem::forget(poison_when_dropped);
                    break;
                }
            };
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
        for semaphore in &self.scope.pool.start_semaphores {
            semaphore.post();
        }
    }

    /*
    pub fn run_with_slice<T>(self, slice: &mut [T], f: impl TaskFn + 'scope) {
        let f = Box::new(f);

        // SAFETY: the closure f has a lifetime of at least the scope's lifetime 'scope
        // SAFETY: TODO
        let f = unsafe { std::mem::transmute::<Box<dyn TaskFn>, Box<dyn TaskFn + 'static>>(f) };

        *self.scope.pool.thread_control.task.borrow_mut() = Some(f);
        //self.scope.pool.thread_control.start_barrier.count_down();
        self.scope.pool.start_counter.count_down();
    }
    */
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
