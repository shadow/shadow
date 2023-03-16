use std::sync::Arc;

use criterion::{criterion_group, criterion_main, Bencher, Criterion};
use nix::sched::CpuSet;
use nix::unistd::Pid;
use vasi_sync::scchannel::SelfContainedChannel;

/// Mock-up of Ipc between Shadow and its plugin.
/// We specify a large alignment here to get consistent cache interactions or
/// not between the atomics inside the channels for benchmarking purposes. i.e.
/// 128 should be large enough to ensure the struct is aligned to the beginning
/// of a cache line.
#[repr(align(128))]
struct Ipc(SelfContainedChannel<()>, SelfContainedChannel<()>);

fn ping_pong(bencher: &mut Bencher, do_pinning: bool) {
    let initial_cpu_set = nix::sched::sched_getaffinity(Pid::from_raw(0)).unwrap();
    let pinned_cpu_id = (0..)
        .into_iter()
        .find(|i| initial_cpu_set.is_set(*i).unwrap())
        .unwrap();
    let pinned_cpu_set = {
        let mut s = CpuSet::new();
        s.set(pinned_cpu_id).unwrap();
        s
    };
    if do_pinning {
        nix::sched::sched_setaffinity(Pid::from_raw(0), &pinned_cpu_set).unwrap();
    }

    let ipc = Arc::new(Ipc(
        SelfContainedChannel::new(),
        SelfContainedChannel::new(),
    ));

    let receiver_thread = {
        let ipc = ipc.clone();
        std::thread::spawn(move || {
            if do_pinning {
                nix::sched::sched_setaffinity(Pid::from_raw(0), &pinned_cpu_set).unwrap();
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
        nix::sched::sched_setaffinity(Pid::from_raw(0), &initial_cpu_set).unwrap();
    }
}

pub fn criterion_benchmark(c: &mut Criterion) {
    c.bench_function("ping pong", |b| ping_pong(b, false));
    c.bench_function("ping pong pinned", |b| ping_pong(b, true));
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
