/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::error::Error;
use std::sync::atomic::{AtomicI32, Ordering};

use test_utils::TestEnvironment as TestEnv;
use test_utils::set;

const NUM_THREADS: usize = 5;

enum ThreadRetVal {
    Success,
    MutexLockFailed,
    MutexUnlockFailed,
    CondWaitFailed,
    CondBroadcastFailed,
    NullThreadArg,
}

impl std::fmt::Display for ThreadRetVal {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let msg = match self {
            ThreadRetVal::Success => "success",
            ThreadRetVal::MutexLockFailed => "pthread_mutex_lock failed",
            ThreadRetVal::MutexUnlockFailed => "pthread_mutex_unlock failed",
            ThreadRetVal::CondWaitFailed => "pthread_cond_wait failed",
            ThreadRetVal::CondBroadcastFailed => "pthread_cond_broadcast failed",
            ThreadRetVal::NullThreadArg => "null thread argument",
        };
        write!(f, "{}", msg)
    }
}

impl TryFrom<u32> for ThreadRetVal {
    type Error = &'static str;

    fn try_from(v: u32) -> Result<Self, Self::Error> {
        match v {
            x if x == ThreadRetVal::Success as u32 => Ok(ThreadRetVal::Success),
            x if x == ThreadRetVal::MutexLockFailed as u32 => Ok(ThreadRetVal::MutexLockFailed),
            x if x == ThreadRetVal::MutexUnlockFailed as u32 => Ok(ThreadRetVal::MutexUnlockFailed),
            x if x == ThreadRetVal::CondWaitFailed as u32 => Ok(ThreadRetVal::CondWaitFailed),
            x if x == ThreadRetVal::CondBroadcastFailed as u32 => {
                Ok(ThreadRetVal::CondBroadcastFailed)
            }
            x if x == ThreadRetVal::NullThreadArg as u32 => Ok(ThreadRetVal::NullThreadArg),
            _ => Err("Unknown enum variant"),
        }
    }
}

struct MuxSum {
    mux: libc::pthread_mutex_t,
    pub sum: u32,
}

impl MuxSum {
    /// Create a new MuxSum with allocated pthread_mux_t and *mut u32 vars
    pub fn new() -> Result<Self, String> {
        let mut rv = Self {
            mux: unsafe { std::mem::zeroed() },
            sum: 0,
        };
        mutex_init(rv.mux_ptr())?;
        Ok(rv)
    }

    /// Get a raw mutable pointer to the inner pthread_mutex_t
    #[inline(always)]
    pub fn mux_ptr(&mut self) -> *mut libc::pthread_mutex_t {
        std::ptr::from_mut(&mut self.mux)
    }
}

impl Drop for MuxSum {
    fn drop(&mut self) {
        mutex_destroy(self.mux_ptr());
    }
}

struct MuxTry {
    mux1: libc::pthread_mutex_t,
    mux2: libc::pthread_mutex_t,
    cond: libc::pthread_cond_t,
    pub num_locked: u32,
    pub num_not_locked: u32,
}

impl MuxTry {
    /// Create a new MuxTry with allocated pthread_mux_t and pthread_cond_t vars
    ///
    /// Returns error message if allocation fails
    pub fn new() -> Result<Self, String> {
        let mut rv = Self {
            mux1: unsafe { std::mem::zeroed() },
            mux2: unsafe { std::mem::zeroed() },
            cond: unsafe { std::mem::zeroed() },
            num_locked: 0,
            num_not_locked: 0,
        };
        mutex_init(rv.mux1_ptr())?;
        mutex_init(rv.mux2_ptr())?;
        cond_init(rv.cond_ptr())?;
        Ok(rv)
    }

    /// Returns a mutable pointer to first inner pthread_mutex_t
    #[inline(always)]
    pub fn mux1_ptr(&mut self) -> *mut libc::pthread_mutex_t {
        std::ptr::from_mut(&mut self.mux1)
    }

    /// Returns a mutable pointer to second inner pthread_mutex_t
    #[inline(always)]
    pub fn mux2_ptr(&mut self) -> *mut libc::pthread_mutex_t {
        std::ptr::from_mut(&mut self.mux2)
    }

    /// Returns a mutable pointer to second inner pthread_mutex_t
    #[inline(always)]
    pub fn cond_ptr(&mut self) -> *mut libc::pthread_cond_t {
        std::ptr::from_mut(&mut self.cond)
    }
}

impl Drop for MuxTry {
    fn drop(&mut self) {
        mutex_destroy(self.mux1_ptr());
        mutex_destroy(self.mux2_ptr());
        cond_destroy(self.cond_ptr());
    }
}

pub struct Attr {
    attr: libc::pthread_attr_t,
}

impl Attr {
    /// Create a new Attr with allocated pthread_attr_t variable
    ///
    /// Returns error message if allocation fails
    pub fn new() -> Result<Self, String> {
        let mut rv = Self {
            attr: unsafe { std::mem::zeroed() },
        };
        attr_init(rv.ptr())?;
        Ok(rv)
    }

    /// Returns a mutable pointer to the inner pthread_attr_t
    #[inline(always)]
    pub fn ptr(&mut self) -> *mut libc::pthread_attr_t {
        std::ptr::from_mut(&mut self.attr)
    }
}

impl Drop for Attr {
    fn drop(&mut self) {
        attr_destroy(self.ptr());
    }
}

// Initialize an allocated pthread_attr_t
//
// Returns error if pointer is null or pthread_attr_init fails
//
// Undefined behavior if called on an already initialized pthread_attr_t,
// without first calling attr_destroy
fn attr_init(ptr: *mut libc::pthread_attr_t) -> Result<(), String> {
    if ptr.is_null() {
        Err("null pthread_mutex_t pointer".into())
    } else if unsafe { libc::pthread_attr_init(ptr) } < 0 {
        Err("pthread_attr_init failed".into())
    } else {
        Ok(())
    }
}

// Destroy a pthread_attr_t
fn attr_destroy(ptr: *mut libc::pthread_attr_t) {
    if !ptr.is_null() {
        unsafe { libc::pthread_attr_destroy(ptr) };
    }
}

// Initialize an allocated pthread_mutex_t
//
// Returns error if pointer is null or pthread_mutex_init fails
//
// Undefined behavior if called on an already initialized pthread_mutex_t,
// without first calling mutex_destroy
fn mutex_init(ptr: *mut libc::pthread_mutex_t) -> Result<(), String> {
    if ptr.is_null() {
        Err("null pthread_mutex_t pointer".into())
    } else if unsafe { libc::pthread_mutex_init(ptr, std::ptr::null_mut()) } < 0 {
        Err("pthread_mutex_init failed".into())
    } else {
        Ok(())
    }
}

// Destroy a pthread_mutex_t
fn mutex_destroy(ptr: *mut libc::pthread_mutex_t) {
    if !ptr.is_null() {
        unsafe { libc::pthread_mutex_destroy(ptr) };
    }
}

// Initialize an allocated pthread_cond_t
//
// Returns error if pointer is null or pthread_cond_init fails
//
// Undefined behavior if called on an already initialized pthread_cond_t,
// without first calling cond_destroy
fn cond_init(ptr: *mut libc::pthread_cond_t) -> Result<(), String> {
    if ptr.is_null() {
        Err("null pthread_cond_t pointer".into())
    } else if unsafe { libc::pthread_cond_init(ptr, std::ptr::null_mut()) } < 0 {
        Err("pthread_cond_init failed".into())
    } else {
        Ok(())
    }
}

// Destroy a pthread_cond_t
fn cond_destroy(ptr: *mut libc::pthread_cond_t) {
    if !ptr.is_null() {
        unsafe { libc::pthread_cond_destroy(ptr) };
    }
}

// Check for error values returned from pthread test functions
fn check_pthread_error(rv: ThreadRetVal) -> Result<(), String> {
    match rv {
        ThreadRetVal::Success => Ok(()),
        _ => Err(rv.to_string()),
    }
}

extern "C" fn make_detached(arg: *mut libc::c_void) -> *mut libc::c_void {
    let p = arg as *mut AtomicI32;
    unsafe { (*p).fetch_add(1, Ordering::SeqCst) };
    arg
}

fn test_make_detached() -> Result<(), String> {
    let mut attr = Attr::new()?;

    if unsafe { libc::pthread_attr_setdetachstate(attr.ptr(), libc::PTHREAD_CREATE_DETACHED) } < 0 {
        return Err("pthread_attr_setdetachstate failed".into());
    }

    let mut thread = libc::pthread_t::default();
    let mut thread_counter = AtomicI32::new(0_i32);

    let rv = unsafe {
        libc::pthread_create(
            &mut thread,
            attr.ptr(),
            make_detached,
            std::ptr::from_mut(&mut thread_counter) as *mut libc::c_void,
        )
    };
    if rv != 0 {
        return Err("pthread_create failed".into());
    }

    while thread_counter.load(Ordering::SeqCst) == 0 {
        std::thread::sleep(std::time::Duration::from_millis(1));
    }

    /* success! */
    Ok(())
}

extern "C" fn thread_return_one(_arg: *mut libc::c_void) -> *mut libc::c_void {
    1 as *mut libc::c_void
}

fn test_make_joinable() -> Result<(), String> {
    let mut threads = [libc::pthread_t::default(); NUM_THREADS];
    let mut attr = Attr::new()?;

    if unsafe { libc::pthread_attr_setdetachstate(attr.ptr(), libc::PTHREAD_CREATE_JOINABLE) } < 0 {
        return Err("pthread_attr_setdetachstate failed".into());
    }

    let null_ptr: *mut libc::c_void = std::ptr::null_mut();

    /* create / send threads */
    for thread in threads.iter_mut() {
        if unsafe { libc::pthread_create(thread, attr.ptr(), thread_return_one, null_ptr) } < 0 {
            return Err("pthread_create failed".into());
        }
    }

    let mut rv = ThreadRetVal::Success as libc::intptr_t;

    /* try to join the threads, checking return value */
    for thread in threads.iter_mut() {
        unsafe {
            libc::pthread_join(
                *thread,
                std::ptr::from_mut(&mut rv) as *mut *mut libc::c_void,
            )
        };
        if rv != 1 {
            return Err("pthread_join did not return one".into());
        }
    }

    /* success! */
    Ok(())
}

extern "C" fn thread_mutex_lock(data: *mut libc::c_void) -> *mut libc::c_void {
    let mut rv = ThreadRetVal::Success;

    let ms = data as *mut MuxSum;

    if ms.is_null() {
        rv = ThreadRetVal::NullThreadArg;
        return rv as u32 as *mut libc::c_void;
    }

    if unsafe { libc::pthread_mutex_lock((*ms).mux_ptr()) } < 0 {
        rv = ThreadRetVal::MutexLockFailed;
        return rv as u32 as *mut libc::c_void;
    }

    let current = unsafe { (*ms).sum };
    if current == 0 {
        unsafe { (*ms).sum = 2 };
    } else {
        /* sum could have changed since time of check
         * assign using stored value, clobbering other possible writes
         */
        unsafe { (*ms).sum = current + 2 };
    }

    if unsafe { libc::pthread_mutex_unlock((*ms).mux_ptr()) } < 0 {
        rv = ThreadRetVal::MutexUnlockFailed;
        return rv as u32 as *mut libc::c_void;
    }

    rv as u32 as *mut libc::c_void
}

fn test_mutex_lock() -> Result<(), String> {
    let mut threads = [libc::pthread_t::default(); NUM_THREADS];
    let mut ms = MuxSum::new()?;

    /* create / send threads */
    for thread in threads.iter_mut() {
        let error = unsafe {
            libc::pthread_create(
                thread,
                std::ptr::null_mut(),
                thread_mutex_lock,
                std::ptr::from_mut(&mut ms) as *mut libc::c_void,
            )
        };
        if error < 0 {
            return Err("pthread_create failed!".into());
        }
    }

    let mut rv = ThreadRetVal::Success as libc::intptr_t;

    /* join threads, check their exit values */
    for thread in threads.iter_mut() {
        if unsafe {
            libc::pthread_join(
                *thread,
                std::ptr::from_mut(&mut rv) as *mut *mut libc::c_void,
            )
        } < 0
        {
            return Err("pthread_join failed!".into());
        }
        check_pthread_error(ThreadRetVal::try_from(rv as u32)?)?;
    }

    let mut expected = 2;
    for _ in 1..NUM_THREADS {
        expected += 2;
    }

    if ms.sum != expected {
        return Err(format!("expected '{}', sum '{}'", expected, ms.sum));
    }

    /* success! */
    Ok(())
}

extern "C" fn thread_mutex_trylock(mx: *mut libc::c_void) -> *mut libc::c_void {
    let mut rv = ThreadRetVal::Success;

    if mx.is_null() {
        rv = ThreadRetVal::NullThreadArg;
        return rv as u32 as *mut libc::c_void;
    }

    /* Track the number of threads that pass the lock. Should be < NUM_THREADS */
    let muxes = mx as *mut MuxTry;

    /* Attempt to lock the mutex */
    if unsafe { libc::pthread_mutex_trylock((*muxes).mux1_ptr()) } == 0 {
        if unsafe { libc::pthread_mutex_lock((*muxes).mux2_ptr()) } < 0 {
            rv = ThreadRetVal::MutexLockFailed;
        }

        unsafe { (*muxes).num_locked += 1 };

        let num_not_locked = unsafe { (*muxes).num_not_locked };
        if num_not_locked == 0
            && NUM_THREADS != 1
            && unsafe { libc::pthread_cond_wait((*muxes).cond_ptr(), (*muxes).mux2_ptr()) } < 0
        {
            rv = ThreadRetVal::CondWaitFailed;
        }

        if unsafe { libc::pthread_mutex_unlock((*muxes).mux2_ptr()) } < 0 {
            rv = ThreadRetVal::MutexUnlockFailed;
        }

        if unsafe { libc::pthread_mutex_unlock((*muxes).mux1_ptr()) } < 0 {
            rv = ThreadRetVal::MutexUnlockFailed;
        }
    } else {
        if unsafe { libc::pthread_mutex_lock((*muxes).mux2_ptr()) } < 0 {
            rv = ThreadRetVal::MutexLockFailed;
        }

        unsafe { (*muxes).num_not_locked += 1 };

        if unsafe { libc::pthread_cond_broadcast((*muxes).cond_ptr()) } < 0 {
            rv = ThreadRetVal::CondBroadcastFailed;
        }

        if unsafe { libc::pthread_mutex_unlock((*muxes).mux2_ptr()) } < 0 {
            rv = ThreadRetVal::MutexUnlockFailed;
        }
    }

    rv as u32 as *mut libc::c_void
}

fn test_mutex_trylock() -> Result<(), String> {
    let mut threads = [libc::pthread_t::default(); NUM_THREADS];
    let mut muxes = MuxTry::new()?;

    let null_ptr = std::ptr::null_mut();

    /* create / send threads */
    for thread in threads.iter_mut() {
        let error = unsafe {
            libc::pthread_create(
                thread,
                null_ptr,
                thread_mutex_trylock,
                std::ptr::from_mut(&mut muxes) as *mut libc::c_void,
            )
        };
        if error < 0 {
            return Err("pthread_create failed!".into());
        }
    }

    let mut rv = ThreadRetVal::Success as libc::intptr_t;

    /* join threads, check their exit values */
    for thread in threads.iter_mut() {
        if unsafe {
            libc::pthread_join(
                *thread,
                std::ptr::from_mut(&mut rv) as *mut *mut libc::c_void,
            )
        } < 0
        {
            return Err("pthread_join failed".into());
        }

        check_pthread_error(ThreadRetVal::try_from(rv as u32)?)?;
    }

    if muxes.num_locked == 0 || muxes.num_not_locked == 0 {
        return Err(format!(
            "{} threads locked (expected >= 1), {} threads skipped (expected >= 1)",
            muxes.num_locked, muxes.num_not_locked
        ));
    }

    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");

    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new(
            "test_make_detached",
            test_make_detached,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_make_joinable",
            test_make_joinable,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_mutex_lock",
            test_mutex_lock,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_mutex_trylock",
            test_mutex_trylock,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnv::Shadow));
    }

    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnv::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");

    Ok(())
}
