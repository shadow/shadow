//! This file contains utilities that can be reused across integration tests.
//! It's in a subdirectory of tests to avoid being interpreted as an integration
//! test itself. See
//! https://doc.rust-lang.org/book/ch11-03-test-organization.html#submodules-in-integration-tests

// Items in here may not end up being used by every test.
#![allow(unused)]

#[cfg(loom)]
pub fn model<F>(f: F)
where
    F: Fn() + Sync + Send + 'static,
{
    loom::model(move || {
        f();
        vasi_sync::sync::loom_reset();
    });
}
#[cfg(not(loom))]
pub fn model<F>(f: F)
where
    F: Fn() + Sync + Send + 'static,
{
    f()
}

#[cfg(not(loom))]
pub use std::sync::Arc;
#[cfg(not(loom))]
pub use std::thread;

#[cfg(loom)]
pub use loom::sync::Arc;
#[cfg(loom)]
pub use loom::thread;

#[cfg(loom)]
pub fn rand_sleep() {}
#[cfg(not(loom))]
pub fn rand_sleep() {
    std::thread::sleep(std::time::Duration::from_nanos(
        rand::random::<u64>() % 10_000_000,
    ));
}
