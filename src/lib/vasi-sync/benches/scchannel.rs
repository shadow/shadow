use std::sync::Arc;

use criterion::{Bencher, Criterion, criterion_group, criterion_main};
use rustix::process::{CpuSet, Pid};
use vasi_sync::scchannel::SelfContainedChannel;

/// Mock-up of Ipc between Shadow and its plugin.
/// We specify a large alignment here to get consistent cache interactions or
/// not between the atomics inside the channels for benchmarking purposes. i.e.
/// 128 should be large enough to ensure the struct is aligned to the beginning
/// of a cache line.
#[repr(align(128))]
struct Ipc(SelfContainedChannel<()>, SelfContainedChannel<()>);

const PID_ZERO: Option<Pid> = Pid::from_raw(0);

fn ping_pong(bencher: &mut Bencher, do_pinning: bool) {
    let initial_cpu_set = rustix::process::sched_getaffinity(PID_ZERO).unwrap();
    let pinned_cpu_id = (0..).find(|i| initial_cpu_set.is_set(*i)).unwrap();
    let pinned_cpu_set = {
        let mut s = CpuSet::new();
        s.set(pinned_cpu_id);
        s
    };
    if do_pinning {
        rustix::process::sched_setaffinity(PID_ZERO, &pinned_cpu_set).unwrap();
    }

    let ipc = Arc::new(Ipc(
        SelfContainedChannel::new(),
        SelfContainedChannel::new(),
    ));

    let receiver_thread = {
        let ipc = ipc.clone();
        std::thread::spawn(move || {
            if do_pinning {
                rustix::process::sched_setaffinity(PID_ZERO, &pinned_cpu_set).unwrap();
            }
            loop {
                if ipc.0.receive().is_err() {
                    break;
                }
                ipc.1.send(());
            }
        })
    };

    bencher.iter(|| {
        ipc.0.send(());
        ipc.1.receive().unwrap();
    });

    ipc.0.close_writer();
    receiver_thread.join().unwrap();
    if do_pinning {
        rustix::process::sched_setaffinity(PID_ZERO, &initial_cpu_set).unwrap();
    }
}

pub fn criterion_benchmark(c: &mut Criterion) {
    c.bench_function("ping pong", |b| ping_pong(b, false));
    c.bench_function("ping pong pinned", |b| ping_pong(b, true));
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
