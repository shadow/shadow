use libc::{siginfo_t, stack_t};
use nix::sys::signal::Signal;
use shadow_shmem::allocator::ShMemBlockSerialized;
use vasi::VirtualAddressSpaceIndependent;
use vasi_sync::scmutex::SelfContainedMutex;

use crate::option::FfiOption;
use crate::HostId;
use crate::{
    emulated_time::{AtomicEmulatedTime, EmulatedTime},
    rootedcell::{refcell::RootedRefCell, Root},
    signals::{
        shd_kernel_sigaction, shd_kernel_sigset_t, SHD_SIGRT_MAX, SHD_STANDARD_SIGNAL_MAX_NO,
    },
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

    // Current simulation time.
    pub sim_time: AtomicEmulatedTime,
}
assert_shmem_safe!(HostShmem, _hostshmem_test_fn);

impl HostShmem {
    pub fn new(
        host_id: HostId,
        model_unblocked_syscall_latency: bool,
        max_unapplied_cpu_latency: SimulationTime,
        unblocked_syscall_latency: SimulationTime,
        unblocked_vdso_latency: SimulationTime,
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
            sim_time: AtomicEmulatedTime::new(EmulatedTime::MIN),
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
                    pending_signals: shd_kernel_sigset_t::EMPTY,
                    pending_standard_siginfos: [SiginfoWrapper::new();
                        SHD_STANDARD_SIGNAL_MAX_NO as usize],
                    signal_actions: [shd_kernel_sigaction::default(); SHD_SIGRT_MAX as usize],
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
    pub pending_signals: shd_kernel_sigset_t,

    // siginfo for each of the standard signals.
    pending_standard_siginfos: [SiginfoWrapper; SHD_STANDARD_SIGNAL_MAX_NO as usize],

    // actions for both standard and realtime signals.
    // We currently support configuring handlers for realtime signals, but not
    // actually delivering them. This is to handle the case where handlers are
    // defensively installed, but not used in practice.
    signal_actions: [shd_kernel_sigaction; SHD_SIGRT_MAX as usize],
}

impl ProcessShmemProtected {
    pub fn pending_standard_siginfo(&self, signal: Signal) -> Option<&SiginfoWrapper> {
        if self.pending_signals.has(signal) {
            Some(&self.pending_standard_siginfos[signal as usize - 1])
        } else {
            None
        }
    }

    pub fn set_pending_standard_siginfo(&mut self, signal: Signal, info: &siginfo_t) {
        assert!(self.pending_signals.has(signal));
        self.pending_standard_siginfos[signal as usize - 1] = (*info).into();
    }

    /// # Safety
    ///
    /// Function pointers in `shd_kernel_sigaction::u` are valid only
    /// from corresponding managed process, and may be libc::SIG_DFL or
    /// libc::SIG_IGN.
    pub unsafe fn signal_action(&self, signal: Signal) -> &shd_kernel_sigaction {
        &self.signal_actions[signal as usize - 1]
    }

    /// # Safety
    ///
    /// Function pointers in `shd_kernel_sigaction::u` are valid only
    /// from corresponding managed process, and may be libc::SIG_DFL or
    /// libc::SIG_IGN.
    pub unsafe fn signal_action_mut(&mut self, signal: Signal) -> &mut shd_kernel_sigaction {
        &mut self.signal_actions[signal as usize - 1]
    }

    pub fn take_pending_unblocked_signal(
        &mut self,
        thread: &ThreadShmemProtected,
    ) -> Option<(Signal, SiginfoWrapper)> {
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
    /// Handle to shared memory for the Process
    pub process_shmem: ShMemBlockSerialized,
    tid: libc::pid_t,

    pub protected: RootedRefCell<ThreadShmemProtected>,
}
assert_shmem_safe!(ThreadShmem, _test_threadshmem_fn);

impl ThreadShmem {
    pub fn new(
        host: &HostShmemProtected,
        process_shmem: ShMemBlockSerialized,
        tid: libc::pid_t,
    ) -> Self {
        Self {
            host_id: host.host_id,
            process_shmem,
            tid,
            protected: RootedRefCell::new(
                &host.root,
                ThreadShmemProtected {
                    host_id: host.host_id,
                    pending_signals: shd_kernel_sigset_t::EMPTY,
                    pending_standard_siginfos: [SiginfoWrapper::new();
                        SHD_STANDARD_SIGNAL_MAX_NO as usize],
                    blocked_signals: shd_kernel_sigset_t::EMPTY,
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
    pub pending_signals: shd_kernel_sigset_t,

    // siginfo for each of the 32 standard signals.
    pending_standard_siginfos: [SiginfoWrapper; SHD_STANDARD_SIGNAL_MAX_NO as usize],

    // Signal mask, e.g. as set by `sigprocmask`.
    // We don't use sigset_t since glibc uses a much larger bitfield than
    // actually supported by the kernel.
    pub blocked_signals: shd_kernel_sigset_t,

    // Configured alternate signal stack for this thread.
    sigaltstack: StackWrapper,
}

impl ThreadShmemProtected {
    pub fn pending_standard_siginfo(&self, signal: Signal) -> Option<&SiginfoWrapper> {
        if self.pending_signals.has(signal) {
            Some(&self.pending_standard_siginfos[signal as usize - 1])
        } else {
            None
        }
    }

    pub fn set_pending_standard_siginfo(&mut self, signal: Signal, info: &SiginfoWrapper) {
        assert!(self.pending_signals.has(signal));
        self.pending_standard_siginfos[signal as usize - 1] = *info;
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

    pub fn take_pending_unblocked_signal(&mut self) -> Option<(Signal, SiginfoWrapper)> {
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

#[derive(Debug, Copy, Clone)]
#[repr(transparent)]
pub struct SiginfoWrapper(libc::siginfo_t);

// SAFETY: We ensure the contained pointers aren't dereferenced except from the
// original virtual address space.
//
// libc::siginfo_t currently doesn't expose the pointer fields at all, but that
// could change in the future. This wrapper will never expose them (unless via
// `unsafe` methods)
unsafe impl VirtualAddressSpaceIndependent for SiginfoWrapper {}

impl SiginfoWrapper {
    pub fn new() -> Self {
        // SAFETY: any bit pattern is a sound value of `siginfo_t`.
        // TODO: Move the Pod trait out of shadow_rs and use it here.
        Self(unsafe { std::mem::zeroed() })
    }

    pub fn signo(&self) -> &libc::c_int {
        &self.0.si_signo
    }

    pub fn signo_mut(&mut self) -> &mut libc::c_int {
        &mut self.0.si_signo
    }

    pub fn signal(&self) -> Option<Signal> {
        if self.signo() == &0 {
            None
        } else {
            Some(signal_from_i32(*self.signo()))
        }
    }

    pub fn errno(&self) -> &libc::c_int {
        &self.0.si_errno
    }

    pub fn errno_mut(&mut self) -> &mut libc::c_int {
        &mut self.0.si_errno
    }

    pub fn code(&self) -> &libc::c_int {
        &self.0.si_code
    }

    pub fn code_mut(&mut self) -> &mut libc::c_int {
        &mut self.0.si_code
    }

    /// # Safety
    ///
    /// Pointer fields must not be dereferenced from outside of their
    /// native virtual address space.
    pub unsafe fn as_siginfo(&self) -> &libc::siginfo_t {
        &self.0
    }
}

impl Default for SiginfoWrapper {
    fn default() -> Self {
        Self::new()
    }
}

impl From<libc::siginfo_t> for SiginfoWrapper {
    fn from(s: libc::siginfo_t) -> Self {
        Self(s)
    }
}

impl<'a> From<&'a libc::siginfo_t> for &'a SiginfoWrapper {
    fn from(s: &libc::siginfo_t) -> &SiginfoWrapper {
        // SAFETY: SiginfoWrapper is a repr[transparent] wrapper
        unsafe { &*(s as *const _ as *const SiginfoWrapper) }
    }
}

// FIXME: temporary workaround for nix's lack of support for realtime
// signals.
fn signal_from_i32(s: i32) -> Signal {
    assert!(s <= libc::SIGRTMAX());
    unsafe { std::mem::transmute(s) }
}

pub mod export {
    use std::sync::atomic::Ordering;

    use vasi_sync::scmutex::SelfContainedMutexGuard;

    use super::*;
    use crate::{emulated_time::CEmulatedTime, simulation_time::CSimulationTime};

    // Legacy type names; keeping the more verbose names for the C API, since
    // they're not namespaced.
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

    #[no_mangle]
    pub extern "C" fn shimshmemprocess_size() -> usize {
        std::mem::size_of::<ProcessShmem>()
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
    ) -> shd_kernel_sigset_t {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = process_mem.protected.borrow(&lock.root);
        protected.pending_signals
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
        s: shd_kernel_sigset_t,
    ) {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = process_mem.protected.borrow_mut(&lock.root);
        protected.pending_signals = s;
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
    ) -> siginfo_t {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = process_mem.protected.borrow(&lock.root);
        // SAFETY: Caller is responsible for not dereferencing the pointers outside the original
        // virtual address space.
        unsafe {
            *protected
                .pending_standard_siginfo(signal_from_i32(sig))
                .unwrap()
                .as_siginfo()
        }
    }

    /// Set the siginfo for the given signal number.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setProcessSiginfo(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        sig: i32,
        info: *const siginfo_t,
    ) {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = process_mem.protected.borrow_mut(&lock.root);
        let info = unsafe { info.as_ref().unwrap() };
        protected.set_pending_standard_siginfo(signal_from_i32(sig), info);
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getSignalAction(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        sig: i32,
    ) -> shd_kernel_sigaction {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = process_mem.protected.borrow(&lock.root);
        *unsafe { protected.signal_action(signal_from_i32(sig)) }
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setSignalAction(
        lock: *const ShimShmemHostLock,
        process: *const ShimShmemProcess,
        sig: i32,
        action: *const shd_kernel_sigaction,
    ) {
        let process_mem = unsafe { process.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = process_mem.protected.borrow_mut(&lock.root);
        unsafe { *protected.signal_action_mut(signal_from_i32(sig)) = *action };
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
    ) -> shd_kernel_sigset_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = thread_mem.protected.borrow(&lock.root);
        protected.pending_signals
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
        s: shd_kernel_sigset_t,
    ) {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = thread_mem.protected.borrow_mut(&lock.root);
        protected.pending_signals = s;
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
    ) -> siginfo_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = thread_mem.protected.borrow(&lock.root);
        // SAFETY: Caller ensures pointers aren't derefenced outside of the correct virtual address space.
        unsafe {
            *protected
                .pending_standard_siginfo(signal_from_i32(sig))
                .unwrap()
                .as_siginfo()
        }
    }

    /// Set the siginfo for the given signal number.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_setThreadSiginfo(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
        sig: i32,
        info: *const siginfo_t,
    ) {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = thread_mem.protected.borrow_mut(&lock.root);
        let info = unsafe { info.as_ref().unwrap() };
        protected.set_pending_standard_siginfo(signal_from_i32(sig), info.into());
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getBlockedSignals(
        lock: *const ShimShmemHostLock,
        thread: *const ShimShmemThread,
    ) -> shd_kernel_sigset_t {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let protected = thread_mem.protected.borrow(&lock.root);
        protected.blocked_signals
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
        s: shd_kernel_sigset_t,
    ) {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        let lock = unsafe { lock.as_ref().unwrap() };
        let mut protected = thread_mem.protected.borrow_mut(&lock.root);
        protected.blocked_signals = s;
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
        info: *mut siginfo_t,
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
                unsafe { info.write(*info_res.as_siginfo()) };
            }
            signal as i32
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

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable. The returned pointer is
    /// borrowed from `process`.
    #[no_mangle]
    pub unsafe extern "C" fn shimshmem_getProcessShmem(
        thread: *const ShimShmemThread,
    ) -> *const ShMemBlockSerialized {
        let thread_mem = unsafe { thread.as_ref().unwrap() };
        &thread_mem.process_shmem
    }
}
