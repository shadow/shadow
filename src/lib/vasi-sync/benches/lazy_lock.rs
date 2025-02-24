use criterion::{Criterion, criterion_group, criterion_main};
use vasi_sync::lazy_lock::LazyLock;

const ITERATIONS: usize = 10_000;

fn no_contention() -> u32 {
    static L: LazyLock<u32> = LazyLock::const_new(|| 42);
    let mut sum = 0;
    for _ in 0..ITERATIONS {
        sum += *L.force();
    }
    sum
}

fn contention() -> u32 {
    static L: LazyLock<u32> = LazyLock::const_new(|| 42);
    let thread_fn = || {
        let mut sum = 0;
        for _ in 0..ITERATIONS {
            sum += *L.force();
        }
        sum
    };
    let t1 = std::thread::spawn(thread_fn);
    let t2 = std::thread::spawn(thread_fn);
    t1.join().unwrap() + t2.join().unwrap()
}

// This basically simulates a more naive implementation using a Mutex.  LazyLock
// should be (and is) substantially faster because there are no writes to its
// atomic after initialization, vs a Mutex which writes to the atomic every time
// the Mutex is taken and released.
fn contention_mutex() -> u32 {
    static L: std::sync::Mutex<u32> = std::sync::Mutex::new(0);

    let thread_fn = || {
        let mut sum = 0;
        for _ in 0..ITERATIONS {
            let value = {
                let mut l = L.lock().unwrap();
                if *l == 0 {
                    *l = 42;
                }
                *l
            };
            sum += value;
        }
        sum
    };
    let t1 = std::thread::spawn(thread_fn);
    let t2 = std::thread::spawn(thread_fn);
    t1.join().unwrap() + t2.join().unwrap()
}

pub fn criterion_benchmark(c: &mut Criterion) {
    c.bench_function("lazy_lock_no_contention", |b| b.iter(no_contention));
    c.bench_function("lazy_lock_contention", |b| b.iter(contention));
    c.bench_function("lazy_lock_mutex", |b| b.iter(contention_mutex));
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
