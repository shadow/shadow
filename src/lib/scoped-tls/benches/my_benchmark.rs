use std::cell::Cell;

use criterion::{black_box, criterion_group, criterion_main, Criterion};

use scoped_tls::{ScopedTls, ScopedTlsPthread};

pub fn criterion_benchmark(c: &mut Criterion) {
    // benchmark based on https://matklad.github.io/2020/10/03/fast-thread-locals-in-rust.html
    const STEPS: u32 = 200;

    thread_local! {
        static THREAD_LOCAL: Cell<u32>  = Cell::new(0);
    }
    c.bench_function("thread_local", |b| {
        b.iter(|| {
            for step in 0..STEPS {
                THREAD_LOCAL.with(|it| {
                    it.set(it.get().wrapping_add(black_box(step)))
                })
            }
            THREAD_LOCAL.with(|it| it.get())
        })
    });

    struct MyMarker(());
    type MyScopedTls = ScopedTls<MyMarker, Cell<u32>>;
    c.bench_function("ScopedTls", |b| {
        b.iter(|| {
            let cell = Cell::new(0);
            MyScopedTls::with_current_set_to(&cell, || {
                for step in 0..STEPS {
                    let it = unsafe { MyScopedTls::current().unwrap_unchecked() };
                    it.set(it.get().wrapping_add(black_box(step)))
                }
                MyScopedTls::current().unwrap().get();
            })
        })
    });

    static SCOPED_TLS_PTHREAD: ScopedTlsPthread<Cell<u32>> = ScopedTlsPthread::new();
    c.bench_function("ScopedTlsPthread", |b| {
        b.iter(|| {
            let cell = Cell::new(0);
            SCOPED_TLS_PTHREAD.with_current_set_to(&cell, || {
                for step in 0..STEPS {
                    let it = unsafe { SCOPED_TLS_PTHREAD.current().unwrap_unchecked() };
                    it.set(it.get().wrapping_add(black_box(step)))
                }
                SCOPED_TLS_PTHREAD.current().unwrap().get()
            })
        })
    });

    c.bench_function("local", |b| {
        b.iter(|| {
            let it: Cell<u32> = Cell::new(0);
            for step in 0..STEPS {
                it.set(it.get().wrapping_add(black_box(step)))
            }
            it.get()
        })
    });
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
