use libc::stack_t;
use linux_api::signal::{sigaction, siginfo_t, sigset_t, Signal};
use shadow_shmem::allocator::{ShMemBlock, ShMemBlockSerialized};
use vasi::VirtualAddressSpaceIndependent;
use vasi_sync::scmutex::SelfContainedMutex;

use crate::option::FfiOption;
use crate::HostId;
use crate::{
    emulated_time::{AtomicEmulatedTime, EmulatedTime},
    rootedcell::{refcell::RootedRefCell, Root},
    simulation_time::SimulationTime,
};

// Validates that a type is safe to store in shared memory.
macro_rules! assert_shmem_safe {
    ($t:ty, $testfnname:ident) => {
        // Must be Sync, since it will be simultaneously available to multiple
        // threads (and processes).
        static_assertions::assert_impl_all!($t: Sync);

        // Must be VirtualAddressSpaceIndpendent, since it may be simultaneously
        // mapped into different virtual address spaces.
        static_assertions::assert_impl_all!($t: VirtualAddressSpaceIndependent);

        // Must have a stable layout.
        // This property is important if it's possible for code compiled in
        // different `rustc` invocations to access the shared memory. Theoretically,
        // with the current Shadow build layout, it *shouldn't* be needed, since this
        // code should be compiled only once before linking into both Shadow and the shim.
        // It would be easy to lose that property without noticing though, and end up with
        // very subtle memory bugs.
        //
        // We could also potentially dispense with this requirement for shared
        // memory only ever accessed via a dynamically linked library. Such a
        // library can only provided C abi public functions though.
        //
        // TODO: Consider instead implementing a trait like FFISafe, with a
        // derive-macro that validates that the type itself has an appropriate
        // `repr`, and that all of its fields are FFISafe. We could then
        // implement a trait `trait IsShmemSafe: Sync +
        // VirtualAddressSpaceIndpendent + FFISafe` instead of this macro
        // `assert_shmem_safe`, and enforce it in e.g. APIs that set up and
        // initialize shared memory.
        #[deny(improper_ctypes_definitions)]
        unsafe extern "C" fn $testfnname(_: $t) {}
    };
}

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ManagerShmem {
    pub log_start_time_micros: i64,
}

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct HostShmem {
    pub host_id: HostId,

    pub protected: SelfContainedMutex<HostShmemProtected>,

    // Whether to model unblocked syscalls as taking non-zero time.
    // TODO: Move to a "ShimShmemGlobal" struct if we make one.
    pub model_unblocked_syscall_latency: bool,

    // Maximum accumulated CPU latency before updating clock.
    // TODO: Move to a "ShimShmemGlobal" struct if we make one, and if this
    // stays a global constant; Or down into the process if we make it a
    // per-process option.
    pub max_unapplied_cpu_latency: SimulationTime,

    // How much to move time forward for each unblocked syscall.
    // TODO: Move to a "ShimShmemGlobal" struct if we make one, and if this
    // stays a global constant; Or down into the process if we make it a
    // per-process option.
    pub unblocked_syscall_latency: SimulationTime,

    // How much to move time forward for each unblocked vdso "syscall".
    // TODO: Move to a "ShimShmemGlobal" struct if we make one, and if this
    // stays a global constant; Or down into the process if we make it a
    // per-process option.
    pub unblocked_vdso_latency: SimulationTime,

    // Native pid of the Shadow simulator process.
    pub shadow_pid: libc::pid_t,

    // Emulated CPU TSC clock rate, for rdtsc emulation.
    pub tsc_hz: u64,

    // Current simulation time.
    pub sim_time: AtomicEmulatedTime,

    pub shim_log_level: logger::LogLevel,

    pub manager_shmem: ShMemBlockSerialized,
}
assert_shmem_safe!(HostShmem, _hostshmem_test_fn);

impl HostShmem {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        host_id: HostId,
        model_unblocked_syscall_latency: bool,
        max_unapplied_cpu_latency: SimulationTime,
        unblocked_syscall_latency: SimulationTime,
        unblocked_vdso_latency: SimulationTime,
        shadow_pid: libc::pid_t,
        tsc_hz: u64,
        shim_log_level: ::logger::LogLevel,
        manager_shmem: &ShMemBlock<ManagerShmem>,
    ) -> Self {
        Self {
            host_id,
            protected: SelfContainedMutex::new(HostShmemProtected {
                host_id,
                root: Root::new(),
                unapplied_cpu_latency: SimulationTime::ZERO,
                max_runahead_time: EmulatedTime::MIN,
            }),
            model_unblocked_syscall_latency,
            max_unapplied_cpu_latency,
            unblocked_syscall_latency,
            unblocked_vdso_latency,
            shadow_pid,
            tsc_hz,
            sim_time: AtomicEmulatedTime::new(EmulatedTime::MIN),
            shim_log_level,
            manager_shmem: manager_shmem.serialize(),
        }
    }

    pub fn protected(&self) -> &SelfContainedMutex<HostShmemProtected> {
        &self.protected
    }
}

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct HostShmemProtected {
    pub host_id: HostId,

    pub root: Root,

    // Modeled CPU latency that hasn't been applied to the clock yet.
    pub unapplied_cpu_latency: SimulationTime,

    // Max simulation time to which sim_time may be incremented.  Moving time
    // beyond this value requires the current thread to be rescheduled.
    pub max_runahead_time: EmulatedTime,
}

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ProcessShmem {
    host_id: HostId,

    /// Handle to shared memory for the Host
    pub host_shmem: ShMemBlockSerialized,
    pub strace_fd: FfiOption<libc::c_int>,

    pub protected: RootedRefCell<ProcessShmemProtected>,
}
assert_shmem_safe!(ProcessShmem, _test_processshmem_fn);

impl ProcessShmem {
    pub fn new(
        host_root: &Root,
        host_shmem: ShMemBlockSerialized,
        host_id: HostId,
        strace_fd: Option<libc::c_int>,
    ) -> Self {
        Self {
            host_id,
            host_shmem,
            strace_fd: strace_fd.into(),
            protected: RootedRefCell::new(
                host_root,
                ProcessShmemProtected {
                    host_id,
                    pending_signals: sigset_t::EMPTY,
                    pending_standard_siginfos: [siginfo_t::default();
                        Signal::STANDARD_MAX.as_i32() as usize],
                    signal_actions: [sigaction::default(); Signal::MAX.as_i32() as usize],
                },
            ),
        }
    }
}

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ProcessShmemProtected {
    pub host_id: HostId,

    // Process-directed pending signals.
    pub pending_signals: sigset_t,

    // siginfo for each of the standard signals.
    // SAFETY: we ensure the internal pointers aren't dereferenced
    // outside of its original virtual address space.
    #[unsafe_assume_virtual_address_space_independent]
    pending_standard_siginfos: [siginfo_t; Signal::STANDARD_MAX.as_i32() as usize],

    // actions for both standard and realtime signals.
    // We currently support configuring handlers for realtime signals, but not
    // actually delivering them. This is to handle the case where handlers are
    // defensively installed, but not used in practice.
    // SAFETY: we ensure the internal pointers aren't dereferenced
    // outside of its original virtual address space.
    #[unsafe_assume_virtual_address_space_independent]
    signal_actions: [sigaction; Signal::MAX.as_i32() as usize],
}

// We have several arrays indexed by signal number - 1.
fn signal_idx(signal: Signal) -> usize {
    (i32::from(signal) - 1) as usize
}

impl ProcessShmemProtected {
    pub fn pending_standard_siginfo(&self, signal: Signal) -> Option<&siginfo_t> {
        if self.pending_signals.has(signal) {
            Some(&self.pending_standard_siginfos[signal_idx(signal)])
        } else {
            None
        }
    }

    pub fn set_pending_standard_siginfo(&mut self, signal: Signal, info: &siginfo_t) {
        assert!(self.pending_signals.has(signal));
        self.pending_standard_siginfos[signal_idx(signal)] = *info;
    }

    /// # Safety
    ///
    /// Function pointers in `shd_kernel_sigaction::u` are valid only
    /// from corresponding managed process, and may be libc::SIG_DFL or
    /// libc::SIG_IGN.
    pub unsafe fn signal_action(&self, signal: Signal) -> &sigaction {
        &self.signal_actions[signal_idx(signal)]
    }

    /// # Safety
    ///
    /// Function pointers in `shd_kernel_sigaction::u` are valid only
    /// from corresponding managed process, and may be libc::SIG_DFL or
    /// libc::SIG_IGN.
    pub unsafe fn signal_action_mut(&mut self, signal: Signal) -> &mut sigaction {
        &mut self.signal_actions[signal_idx(signal)]
    }

    pub fn take_pending_unblocked_signal(
        &mut self,
        thread: &ThreadShmemProtected,
    ) -> Option<(Signal, siginfo_t)> {
        let pending_unblocked_signals = self.pending_signals & !thread.blocked_signals;
        if pending_unblocked_signals.is_empty() {
            None
        } else {
            let signal = pending_unblocked_signals.lowest().unwrap();
            let info = *self.pending_standard_siginfo(signal).unwrap();
            self.pending_signals.del(signal);
            Some((signal, info))
        }
    }
}

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ThreadShmem {
    pub host_id: HostId,
    tid: libc::pid_t,

    pub protected: RootedRefCell<ThreadShmemProtected>,
}
assert_shmem_safe!(ThreadShmem, _test_threadshmem_fn);

impl ThreadShmem {
    pub fn new(host: &HostShmemProtected, tid: libc::pid_t) -> Self {
        Self {
            host_id: host.host_id,
            tid,
            protected: RootedRefCell::new(
                &host.root,
                ThreadShmemProtected {
                    host_id: host.host_id,
                    pending_signals: sigset_t::EMPTY,
                    pending_standard_siginfos: [siginfo_t::default();
                        Signal::STANDARD_MAX.as_i32() as usize],
                    blocked_signals: sigset_t::EMPTY,
                    sigaltstack: StackWrapper(stack_t {
                        ss_sp: std::ptr::null_mut(),
                        ss_flags: libc::SS_DISABLE,
                        ss_size: 0,
                    }),
                },
            ),
        }
    }
}

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ThreadShmemProtected {
    pub host_id: HostId,

    // Thread-directed pending signals.
    pub pending_signals: sigset_t,

    // siginfo for each of the 32 standard signals.
    // SAFETY: we ensure the internal pointers aren't dereferenced
    // outside of its original virtual address space.
    #[unsafe_assume_virtual_address_space_independent]
    pending_standard_siginfos: [siginfo_t; Signal::STANDARD_MAX.as_i32() as usize],

    // Signal mask, e.g. as set by `sigprocmask`.
    // We don't use sigset_t since glibc uses a much larger bitfield than
    // actually supported by the kernel.
    pub blocked_signals: sigset_t,

    // Configured alternate signal stack for this thread.
    sigaltstack: StackWrapper,
}

impl ThreadShmemProtected {
    pub fn pending_standard_siginfo(&self, signal: Signal) -> Option<&siginfo_t> {
        if self.pending_signals.has(signal) {
            Some(&self.pending_standard_siginfos[signal_idx(signal)])
        } else {
            None
        }
    }

    pub fn set_pending_standard_siginfo(&mut self, signal: Signal, info: &siginfo_t) {
        assert!(self.pending_signals.has(signal));
        self.pending_standard_siginfos[signal_idx(signal)] = *info;
    }

    /// # Safety
    ///
    /// `stack_t::ss_sp` must not be dereferenced except from corresponding
    /// managed thread.
    pub unsafe fn sigaltstack(&self) -> &stack_t {
        &self.sigaltstack.0
    }

    /// # Safety
    ///
    /// `stack_t::ss_sp` must not be dereferenced except from corresponding
    /// managed thread. Must be set to either std::ptr::null_mut, or a pointer valid
    /// in the managed thread.
    pub unsafe fn sigaltstack_mut(&mut self) -> &mut stack_t {
        &mut self.sigaltstack.0
    }

    pub fn take_pending_unblocked_signal(&mut self) -> Option<(Signal, siginfo_t)> {
        let pending_unblocked_signals = self.pending_signals & !self.blocked_signals;
        if pending_unblocked_signals.is_empty() {
            None
        } else {
            let signal = pending_unblocked_signals.lowest().unwrap();
            let info = *self.pending_standard_siginfo(signal).unwrap();
            self.pending_signals.del(signal);
            Some((signal, info))
        }
    }
}

#[repr(transparent)]
struct StackWrapper(stack_t);

// SAFETY: We ensure the contained pointer isn't dereferenced
// except from the owning thread.
unsafe impl Send for StackWrapper {}

// SAFETY: We ensure the contained pointers isn't dereferenced
// except from the original virtual address space: in the shim.
unsafe impl VirtualAddressSpaceIndependent for StackWrapper {}

pub mod export {
    use std::sync::atomic::Ordering;

    use bytemuck::TransparentWrapper;
    use linux_api::signal::{linux_sigaction, linux_siginfo_t, linux_sigset_t, siginfo_t};
    use vasi_sync::scmutex::SelfContainedMutexGuard;

    use super::*;
    use crate::{emulated_time::CEmulatedTime, simulation_time::CSimulationTime};

    // Legacy type names; keeping the more verbose names for the C API, since
    // they're not namespaced.
    pub type ShimShmemManager = ManagerShmem;
    pub type ShimShmemHost = HostShmem;
    pub type ShimShmemHostLock = HostShmemProtected;
    pub type ShimShmemProcess = ProcessShmem;
    pub type ShimShmemThread = ThreadShmem;

    #[no_mangle]
    pub extern "C" fn shimshmemhost_size() -> usize {
        std::mem::size_of::<HostShmem>()
    }

    /// # Safety
    ///
    /// `host_mem` must be valid, and no references to `host_mem` may exist.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmemhost_destroy(host_mem: *mut ShimShmemHost) {
        unsafe { std::ptr::drop_in_place(host_mem) };
    }

    /// # Safety
    ///
    /// `host` must be valid. The returned pointer must not be accessed from other threads.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmemhost_lock(
        host: *const ShimShmemHost,
    ) -> *mut ShimShmemHostLock {
        let host = unsafe { host.as_ref().unwrap() };
        let mut guard = host.protected().lock();
        let lock = &mut *guard as *mut _;
        guard.disconnect();
        lock
    }

    /// # Safety
    ///
    /// `host` and `lock` must be valid.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmemhost_unlock(
        host: *const ShimShmemHost,
        lock: *mut *mut ShimShmemHostLock,
    ) {
        let host = unsafe { host.as_ref().unwrap() };
        let guard = SelfContainedMutexGuard::reconnect(&host.protected);
        assert_eq!(host.host_id, guard.host_id);

        let p_lock = unsafe { lock.as_mut().unwrap() };
        *p_lock = std::ptr::null_mut();
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getShadowPid(host_mem: *const ShimShmemHost) -> libc::pid_t {
        let host_mem = unsafe { host_mem.as_ref().unwrap() };
        host_mem.shadow_pid
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getTscHz(host_mem: *const ShimShmemHost) -> u64 {
        let host_mem = unsafe { host_mem.as_ref().unwrap() };
        host_mem.tsc_hz
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getLogLevel(
        host_mem: *const ShimShmemHost,
    ) -> ::logger::LogLevel {
        let host_mem = unsafe { host_mem.as_ref().unwrap() };
        host_mem.shim_log_level
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getEmulatedTime(
        host_mem: *const ShimShmemHost,
    ) -> CEmulatedTime {
        let host_mem = unsafe { host_mem.as_ref().unwrap() };
        EmulatedTime::to_c_emutime(Some(host_mem.sim_time.load(Ordering::Relaxed)))
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setEmulatedTime(
        host_mem: *const ShimShmemHost,
        t: CEmulatedTime,
    ) {
        let host_mem = unsafe { host_mem.as_ref().unwrap() };
        host_mem
            .sim_time
            .store(EmulatedTime::from_c_emutime(t).unwrap(), Ordering::Relaxed);
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getMaxRunaheadTime(
        host_mem: *const ShimShmemHostLock,
    ) -> CEmulatedTime {
        let host_mem = unsafe { host_mem.as_ref().unwrap() };
        EmulatedTime::to_c_emutime(Some(host_mem.max_runahead_time))
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setMaxRunaheadTime(
        host_mem: *mut ShimShmemHostLock,
        t: CEmulatedTime,
    ) {
        let host_mem = unsafe { host_mem.as_mut().unwrap() };
        host_mem.max_runahead_time = EmulatedTime::from_c_emutime(t).unwrap();
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getProcessStraceFd(
        process: *const ShimShmemProcess,
    ) -> libc::c_int {
        let process_mem = unsafe { process.as_ref().unwrap() };
        process_mem.strace_fd.unwrap_or(-1)
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable. The returned pointer is
    /// borrowed from `host`.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getHostManagerShmem(
        host: *const ShimShmemHost,
    ) -> *const ShMemBlockSerialized {
        let host = unsafe { host.as_ref().unwrap() };
        &host.manager_shmem
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable. The returned pointer is
    /// borrowed from `process`.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getProcessHostShmem(
        process: *const ShimShmemProcess,
    ) -> *const ShMemBlockSerialized {
        let process_mem = unsafe { process.as_ref().unwrap() };
        &process_mem.host_shmem
    }

    /// Get the process's pending signal set.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getProcessPendingSignals(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
    ) -> linux_sigset_t {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = process_mem.protected.borrow(&lock.root);
        sigset_t::peel(protected.pending_signals)
    }

    /// Set the process's pending signal set.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setProcessPendingSignals(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        s: linux_sigset_t,
    ) {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = process_mem.protected.borrow_mut(&lock.root);
        protected.pending_signals = sigset_t::wrap(s);
    }

    /// Get the siginfo for the given signal number. Only valid when the signal
    /// is pending for the process.
    ///
    /// # Safety
    ///
    /// Pointers in siginfo_t are only valid in their original virtual address space.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getProcessSiginfo(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        sig: i32,
    ) -> linux_siginfo_t {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = process_mem.protected.borrow(&lock.root);
        unsafe {
            siginfo_t::peel(
                *protected
                    .pending_standard_siginfo(Signal::try_from(sig).unwrap())
                    .unwrap(),
            )
        }
    }

    /// Set the siginfo for the given signal number.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable. The mandatory fields of `info` must be initd.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setProcessSiginfo(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        sig: i32,
        info: *const linux_siginfo_t,
    ) {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = process_mem.protected.borrow_mut(&lock.root);
        let info = unsafe { siginfo_t::wrap_ref_assume_initd(info.as_ref().unwrap()) };
        protected.set_pending_standard_siginfo(Signal::try_from(sig).unwrap(), info);
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getSignalAction(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        sig: i32,
    ) -> linux_sigaction {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = process_mem.protected.borrow(&lock.root);
        unsafe { sigaction::peel(*protected.signal_action(Signal::try_from(sig).unwrap())) }
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setSignalAction(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        sig: i32,
        action: *const linux_sigaction,
    ) {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let action = sigaction::wrap_ref(unsafe { action.as_ref().unwrap() });
        let mut protected = process_mem.protected.borrow_mut(&lock.root);
        unsafe { *protected.signal_action_mut(Signal::try_from(sig).unwrap()) = *action };
    }

    #[no_mangle]
    pub extern "C" fn shimshmemthread_size() -> usize {
        std::mem::size_of::<ThreadShmem>()
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getThreadId(thread: *const ShimShmemThread) -> libc::pid_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        thread_mem.tid
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getThreadPendingSignals(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
    ) -> linux_sigset_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = thread_mem.protected.borrow(&lock.root);
        sigset_t::peel(protected.pending_signals)
    }

    /// Set the process's pending signal set.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setThreadPendingSignals(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
        s: linux_sigset_t,
    ) {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = thread_mem.protected.borrow_mut(&lock.root);
        protected.pending_signals = sigset_t::wrap(s);
    }

    /// Get the siginfo for the given signal number. Only valid when the signal
    /// is pending for the signal.
    ///
    /// # Safety
    ///
    /// Pointers in the returned siginfo_t must not be dereferenced except from the managed
    /// process's virtual address space.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getThreadSiginfo(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
        sig: i32,
    ) -> linux_siginfo_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = thread_mem.protected.borrow(&lock.root);
        unsafe {
            siginfo_t::peel(
                *protected
                    .pending_standard_siginfo(Signal::try_from(sig).unwrap())
                    .unwrap(),
            )
        }
    }

    /// Set the siginfo for the given signal number.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable. The mandatory fields of `info` must be initd.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setThreadSiginfo(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
        sig: i32,
        info: *const linux_siginfo_t,
    ) {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = thread_mem.protected.borrow_mut(&lock.root);
        let info = unsafe { siginfo_t::wrap_ref_assume_initd(info.as_ref().unwrap()) };
        protected.set_pending_standard_siginfo(Signal::try_from(sig).unwrap(), info);
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getBlockedSignals(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
    ) -> linux_sigset_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = thread_mem.protected.borrow(&lock.root);
        sigset_t::peel(protected.blocked_signals)
    }

    /// Set the process's pending signal set.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setBlockedSignals(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
        s: linux_sigset_t,
    ) {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = thread_mem.protected.borrow_mut(&lock.root);
        protected.blocked_signals = sigset_t::wrap(s);
    }

    /// Get the signal stack as set by `sigaltstack(2)`.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getSigAltStack(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
    ) -> stack_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = thread_mem.protected.borrow(&lock.root);
        *unsafe { protected.sigaltstack() }
    }

    /// Set the signal stack as set by `sigaltstack(2)`.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setSigAltStack(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
        stack: stack_t,
    ) {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = thread_mem.protected.borrow_mut(&lock.root);
        *unsafe { protected.sigaltstack_mut() } = stack;
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_takePendingUnblockedSignal(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        thread: *const ShimShmemThread,
        info: *mut linux_siginfo_t,
    ) -> i32 {
        let thread = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut thread_protected = thread.protected.borrow_mut(&lock.root);

        let res = {
            if let Some(r) = thread_protected.take_pending_unblocked_signal() {
                Some(r)
            } else {
                let process = unsafe { process.as_ref().unwrap() };
                let mut process_protected = process.protected.borrow_mut(&lock.root);
                process_protected.take_pending_unblocked_signal(&thread_protected)
            }
        };

        if let Some((signal, info_res)) = res {
            if !info.is_null() {
                unsafe { info.write(siginfo_t::peel(info_res)) };
            }
            signal.into()
        } else {
            0
        }
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_incrementUnappliedCpuLatency(
        lock: *mut ShimShmemHostLock,
        dt: CSimulationTime,
    ) {
        let lock = unsafe { lock.as_mut().unwrap() };
        lock.unapplied_cpu_latency += SimulationTime::from_c_simtime(dt).unwrap();
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getUnappliedCpuLatency(
        lock: *const ShimShmemHostLock,
    ) -> CSimulationTime {
        let lock = unsafe { lock.as_ref().unwrap() };
        SimulationTime::to_c_simtime(Some(lock.unapplied_cpu_latency))
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_resetUnappliedCpuLatency(lock: *mut ShimShmemHostLock) {
        let lock = unsafe { lock.as_mut().unwrap() };
        lock.unapplied_cpu_latency = SimulationTime::ZERO;
    }

    /// Get whether to model latency of unblocked syscalls.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getModelUnblockedSyscallLatency(
        host: *const ShimShmemHost,
    ) -> bool {
        let host = unsafe { host.as_ref().unwrap() };
        host.model_unblocked_syscall_latency
    }

    /// Get the configured maximum unblocked syscall latency to accumulate before
    /// yielding.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_maxUnappliedCpuLatency(
        host: *const ShimShmemHost,
    ) -> CSimulationTime {
        let host = unsafe { host.as_ref().unwrap() };
        SimulationTime::to_c_simtime(Some(host.max_unapplied_cpu_latency))
    }

    /// Get the configured latency to emulate for each unblocked syscall.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_unblockedSyscallLatency(
        host: *const ShimShmemHost,
    ) -> CSimulationTime {
        let host = unsafe { host.as_ref().unwrap() };
        SimulationTime::to_c_simtime(Some(host.unblocked_syscall_latency))
    }

    /// Get the configured latency to emulate for each unblocked vdso "syscall".
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_unblockedVdsoLatency(
        host: *const ShimShmemHost,
    ) -> CSimulationTime {
        let host = unsafe { host.as_ref().unwrap() };
        SimulationTime::to_c_simtime(Some(host.unblocked_vdso_latency))
    }

    /// Get the logging start time
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getLoggingStartTime(
        manager: *const ShimShmemManager,
    ) -> i64 {
        let manager = unsafe { manager.as_ref().unwrap() };
        manager.log_start_time_micros
    }
}
