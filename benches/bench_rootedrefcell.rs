use std::{cell::RefCell, sync::Mutex};

use atomic_refcell::AtomicRefCell;
use criterion::{criterion_group, criterion_main, BatchSize, Criterion};
use objgraph::{refcell::RootedRefCell, Root, RootGuard};

#[inline(never)]
fn rootedrefcell_borrow_mut(lock: &RootGuard, x: &RootedRefCell<i32>) {
    *x.borrow_mut(&lock) += 1;
}

#[inline(never)]
fn mutex_borrow_mut(x: &mut Mutex<i32>) {
    *x.lock().unwrap() += 1;
}

#[inline(never)]
fn parking_lot_mutex_borrow_mut(x: &mut parking_lot::Mutex<i32>) {
    *x.lock() += 1;
}

#[inline(never)]
fn atomicrefcell_borrow_mut(x: &mut AtomicRefCell<i32>) {
    *x.borrow_mut() += 1;
}

#[inline(never)]
fn refcell_borrow_mut(x: &mut RefCell<i32>) {
    *x.borrow_mut() += 1;
}

fn criterion_benchmark(c: &mut Criterion) {
    let root: &'static _ = Box::leak(Box::new(Root::new()));
    let lock: &'static _ = Box::leak(Box::new(root.lock()));

    {
        let mut group = c.benchmark_group("borrow_mut");
        group.bench_function("RootedRefCell", |b| {
            b.iter_batched_ref(
                || RootedRefCell::new(&root, 0),
                |x| rootedrefcell_borrow_mut(&lock, x),
                BatchSize::SmallInput,
            );
        });
        group.bench_function("Mutex", |b| {
            b.iter_batched_ref(|| Mutex::new(0), mutex_borrow_mut, BatchSize::SmallInput);
        });
        group.bench_function("parking_lot::Mutex", |b| {
            b.iter_batched_ref(
                || parking_lot::Mutex::new(0),
                |x| parking_lot_mutex_borrow_mut(x),
                BatchSize::SmallInput,
            );
        });
        group.bench_function("AtomicRefCell", |b| {
            b.iter_batched_ref(
                || AtomicRefCell::new(0),
                |x| atomicrefcell_borrow_mut(x),
                BatchSize::SmallInput,
            );
        });
        group.bench_function("RefCell", |b| {
            b.iter_batched_ref(
                || RefCell::new(0),
                |x| refcell_borrow_mut(x),
                BatchSize::SmallInput,
            );
        });
    }
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
