use criterion::{black_box, criterion_group, criterion_main, BatchSize, Criterion};
use objgraph::{rc::RootedRc, Root, RootGuard};
use std::{rc::Rc, sync::Arc};

#[inline(never)]
fn rootedrc_clone_and_drop(lock: &RootGuard, x: RootedRc<()>) {
    x.clone(lock).safely_drop(lock);
    x.safely_drop(lock);
}

#[inline(never)]
fn arc_clone_and_drop(x: Arc<()>) {
    #[allow(clippy::redundant_clone)]
    let _ = x.clone();
}

#[inline(never)]
fn rc_clone_and_drop(x: Rc<i32>) -> i32 {
    #[allow(clippy::redundant_clone)]
    *x.clone()
}

fn criterion_benchmark(c: &mut Criterion) {
    let root = Root::new();

    {
        let lock = root.lock();
        let mut group = c.benchmark_group("clone and drop");
        group.bench_function("RootedRc", |b| {
            b.iter_batched(
                || RootedRc::new(&root, ()),
                |x| rootedrc_clone_and_drop(&lock, x),
                BatchSize::SmallInput,
            );
        });
        group.bench_function("Arc", |b| {
            b.iter_batched(|| Arc::new(()), arc_clone_and_drop, BatchSize::SmallInput);
        });
        group.bench_function("Rc", |b| {
            b.iter_batched(|| Rc::new(1), rc_clone_and_drop, BatchSize::SmallInput);
        });
    }

    {
        let mut group = c.benchmark_group("cross-core clone");
        const N: usize = 10000;
        group.bench_function("RootedRc", |b| {
            b.iter_batched(
                || {
                    std::thread::spawn(|| {
                        let mut core_ids = core_affinity::get_core_ids().unwrap();
                        // Exclude Current core from tests.
                        let setup_core_id = core_ids.pop().unwrap();
                        core_affinity::set_for_current(setup_core_id);
                        let root = Root::new();
                        let mut v = Vec::new();
                        let lock = root.lock();
                        for _ in 0..black_box(N) {
                            v.push(RootedRc::new(&root, ()));
                            // No atomic operation here, but for consistency with Arc benchmark.
                            let t = v.last().unwrap().clone(&lock);
                            t.safely_drop(&lock);
                        }
                        drop(lock);
                        (root, core_ids, v)
                    })
                    .join()
                    .unwrap()
                },
                |(root, core_ids, v)| {
                    std::thread::spawn(move || {
                        let lock = root.lock();
                        for core_id in core_ids {
                            core_affinity::set_for_current(core_id);
                            for rc in &v {
                                let v = rc.clone(&lock);
                                v.safely_drop(&lock);
                            }
                        }
                        // Safely drop contents of v
                        for rc in v {
                            rc.safely_drop(&lock);
                        }
                    })
                    .join()
                    .unwrap()
                },
                BatchSize::SmallInput,
            );
        });
        group.bench_function("Arc", |b| {
            b.iter_batched(
                || {
                    std::thread::spawn(|| {
                        let mut core_ids = core_affinity::get_core_ids().unwrap();
                        // Exclude Current core from tests.
                        let setup_core_id = core_ids.pop().unwrap();
                        core_affinity::set_for_current(setup_core_id);
                        let mut v = Vec::new();
                        for _ in 0..black_box(N) {
                            v.push(Arc::new(()));
                            // Force an atomic operation on this core.
                            let _ = v.last().unwrap().clone();
                        }
                        (core_ids, v)
                    })
                    .join()
                    .unwrap()
                },
                |(core_ids, v)| {
                    std::thread::spawn(move || {
                        for core_id in core_ids {
                            core_affinity::set_for_current(core_id);
                            for rc in &v {
                                let _ = rc.clone();
                            }
                        }
                    })
                    .join()
                    .unwrap()
                },
                BatchSize::SmallInput,
            );
        });
    }

    /*
    {
        let _lock = root.lock();
        let mut group = c.benchmark_group("drop");
        group.bench_function("RootedRc", |b| {
            b.iter_batched(
                || RootedRc::<(), _>::new(root.tag(), ()),
                |x| drop(x),
                BatchSize::SmallInput,
            );
        });
        group.bench_function("Arc", |b| {
            b.iter_batched(|| Arc::new(()), |x| drop(x), BatchSize::SmallInput);
        });
        group.bench_function("Rc", |b| {
            b.iter_batched(|| Rc::new(()), |x| drop(x), BatchSize::SmallInput);
        });
    }
    */
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
