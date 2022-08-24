// structs defined here are stored in shared memory, loaded both into Shadow and
// into Shadow's managed processes.
//
// We use `repr(C)` to ensure that code in managed process's shim library and
// code in Shadow itself use a consistent layout.
//
// "A note on determinism. The definition above does not guarantee determinism
// between executions of the compiler -- two executions may select different
// layouts, even if all inputs are identical."
// https://rust-lang.github.io/unsafe-code-guidelines/layout/structs-and-tuples.html#default-layout-repr-rust
#![deny(improper_ctypes_definitions)]

// structs defined here should also not contain pointers or references.
// Obviously pointers to something outside of the shared memory region will
// point to the wrong thing.  Even "internal" pointers won't work, since the
// shared region is loaded at different virtual addresses in different
// processes.
//
// Forcing all the structs to implement Copy would help prevent some accidental
// references. We currently can't do it though because we use atomics, which
// aren't Copy.  It also wouldn't catch immutable pointers or references to
// 'static lifetime objects, which (I think) are also allowed to be Copy.

use std::sync::atomic::AtomicU64;

// FIXME
type HostId = u32;
type SimulationTime = u64;
type EmulatedTime = u64;

#[repr(C)]
pub struct HostProtectedSharedMem {
    host_id: HostId,

    // Modeled CPU latency that hasn't been applied to the clock yet.
    unapplied_cpu_latency: SimulationTime,

    // Max simulation time to which sim_time may be incremented.  Moving time
    // beyond this value requires the current thread to be rescheduled.
    max_runahead_time: EmulatedTime,
}
extern "C" fn _test_host_protected_shared_mem(_x: HostProtectedSharedMem) {}

#[repr(C)]
pub struct ShmemHost {
    host_id: HostId,

    // The host lock. Guards _ShimShmemHost.protected,
    // _ShimShmemProcess.protected, and _ShimShmemThread.protected.
    mutex: libc::pthread_mutex_t,

    // Guarded by `mutex`.
    protected: HostProtectedSharedMem,

    // Whether to model unblocked syscalls as taking non-zero time.
    // TODO: Move to a "ShimShmemGlobal" struct if we make one.
    //
    // Thread Safety: immutable after initialization.
    // XXX: enforcement?
    model_unblocked_syscall_latency: bool,

    // Maximum accumulated CPU latency before updating clock.
    // TODO: Move to a "ShimShmemGlobal" struct if we make one, and if this
    // stays a global constant; Or down into the process if we make it a
    // per-process option.
    //
    // Thread Safety: immutable after initialization.
    // XXX: enforcement?
    max_unapplied_cpu_latency: SimulationTime,

    // How much to move time forward for each unblocked syscall.
    // TODO: Move to a "ShimShmemGlobal" struct if we make one, and if this
    // stays a global constant; Or down into the process if we make it a
    // per-process option.
    //
    // Thread Safety: immutable after initialization.
    // XXX: enforcement?
    unblocked_syscall_latency: SimulationTime,

    // How much to move time forward for each unblocked vdso "syscall".
    // TODO: Move to a "ShimShmemGlobal" struct if we make one, and if this
    // stays a global constant; Or down into the process if we make it a
    // per-process option.
    //
    // Thread Safety: immutable after initialization.
    // XXX: enforcement?
    unblocked_vdso_latency: SimulationTime,

    // Current simulation time.
    sim_time: AtomicU64,
}
extern "C" fn _test_shmem_host(_x: ShmemHost) { }