//! This file contains utilities that can be reused across integration tests.
//! It's in a subdirectory of tests to avoid being interpreted as an integration
//! test itself. See
//! https://doc.rust-lang.org/book/ch11-03-test-organization.html#submodules-in-integration-tests

// Items in here may not end up being used by every test.
#![allow(unused)]

pub fn model<F>(f: F)
where
    F: Fn() + Sync + Send + 'static,
{
    #[cfg(loom)]
    loom::model(move || {
        f();
        vasi_sync::sync::loom_reset();
    });
    #[cfg(not(loom))]
    f()
}

/// Overrides the default preemption bound. Useful for tests that
/// are otherwise too slow.
///
/// > In practice, setting the thread pre-emption bound to 2 or 3 is enough to
/// > catch most bugs while significantly reducing the number of possible
/// > executions.
///
/// <https://docs.rs/loom/latest/loom/#large-models>
pub fn model_with_max_preemptions<F>(max_preemptions: usize, f: F)
where
    F: Fn() + Sync + Send + 'static,
{
    #[cfg(loom)]
    {
        let mut builder = loom::model::Builder::default();
        builder.preemption_bound = Some(max_preemptions);
        builder.check(move || {
            f();
            vasi_sync::sync::loom_reset();
        });
    }
    #[cfg(not(loom))]
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
