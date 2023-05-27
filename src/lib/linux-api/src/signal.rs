use bytemuck::TransparentWrapper;
use vasi::VirtualAddressSpaceIndependent;

use crate::bindings;

/// Definition is sometimes missing in the userspace headers.
//
// bindgen fails to bind this one.
// Copied from linux's include/uapi/linux/signal.h.
pub const LINUX_SS_AUTODISARM: i32 = 1 << 31;

// signal names
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[repr(transparent)]
pub struct Signal(i32);

#[derive(Debug, Copy, Clone)]
pub struct SignalFromI32Error(pub i32);

impl TryFrom<i32> for Signal {
    type Error = SignalFromI32Error;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        if (1..=i32::from(Signal::SIGRT_MAX)).contains(&value) {
            Ok(Self(value))
        } else {
            Err(SignalFromI32Error(value))
        }
    }
}

impl From<Signal> for i32 {
    fn from(value: Signal) -> Self {
        value.as_i32()
    }
}

impl Signal {
    pub const SIGHUP: Self = Self(bindings::LINUX_SIGHUP as i32);
    pub const SIGINT: Self = Self(bindings::LINUX_SIGINT as i32);
    pub const SIGQUIT: Self = Self(bindings::LINUX_SIGQUIT as i32);
    pub const SIGILL: Self = Self(bindings::LINUX_SIGILL as i32);
    pub const SIGTRAP: Self = Self(bindings::LINUX_SIGTRAP as i32);
    pub const SIGABRT: Self = Self(bindings::LINUX_SIGABRT as i32);
    pub const SIGBUS: Self = Self(bindings::LINUX_SIGBUS as i32);
    pub const SIGFPE: Self = Self(bindings::LINUX_SIGFPE as i32);
    pub const SIGKILL: Self = Self(bindings::LINUX_SIGKILL as i32);
    pub const SIGUSR1: Self = Self(bindings::LINUX_SIGUSR1 as i32);
    pub const SIGSEGV: Self = Self(bindings::LINUX_SIGSEGV as i32);
    pub const SIGUSR2: Self = Self(bindings::LINUX_SIGUSR2 as i32);
    pub const SIGPIPE: Self = Self(bindings::LINUX_SIGPIPE as i32);
    pub const SIGALRM: Self = Self(bindings::LINUX_SIGALRM as i32);
    pub const SIGTERM: Self = Self(bindings::LINUX_SIGTERM as i32);
    pub const SIGSTKFLT: Self = Self(bindings::LINUX_SIGSTKFLT as i32);
    pub const SIGCHLD: Self = Self(bindings::LINUX_SIGCHLD as i32);
    pub const SIGCONT: Self = Self(bindings::LINUX_SIGCONT as i32);
    pub const SIGSTOP: Self = Self(bindings::LINUX_SIGSTOP as i32);
    pub const SIGTSTP: Self = Self(bindings::LINUX_SIGTSTP as i32);
    pub const SIGTTIN: Self = Self(bindings::LINUX_SIGTTIN as i32);
    pub const SIGTTOU: Self = Self(bindings::LINUX_SIGTTOU as i32);
    pub const SIGURG: Self = Self(bindings::LINUX_SIGURG as i32);
    pub const SIGXCPU: Self = Self(bindings::LINUX_SIGXCPU as i32);
    pub const SIGXFSZ: Self = Self(bindings::LINUX_SIGXFSZ as i32);
    pub const SIGVTALRM: Self = Self(bindings::LINUX_SIGVTALRM as i32);
    pub const SIGPROF: Self = Self(bindings::LINUX_SIGPROF as i32);
    pub const SIGWINCH: Self = Self(bindings::LINUX_SIGWINCH as i32);
    pub const SIGIO: Self = Self(bindings::LINUX_SIGIO as i32);
    pub const SIGPWR: Self = Self(bindings::LINUX_SIGPWR as i32);
    pub const SIGSYS: Self = Self(bindings::LINUX_SIGSYS as i32);

    pub const STANDARD_MAX: Self = Self::SIGSYS;

    pub const SIGRT_MIN: Self = Self(bindings::LINUX_SIGRTMIN as i32);
    // According to signal(7). bindgen fails to bind this one.
    pub const SIGRT_MAX: Self = Self(64);

    pub const MIN: Self = Self(1);
    pub const MAX: Self = Self::SIGRT_MAX;

    const fn const_alias(from: u32, to: Self) -> Self {
        if from as i32 != to.0 {
            // Can't use a format string here since this function is `const`
            panic!("Incorrect alias")
        }
        to
    }
    pub const SIGIOT: Self = Self::const_alias(bindings::LINUX_SIGIOT, Self::SIGABRT);
    pub const SIGPOLL: Self = Self::const_alias(bindings::LINUX_SIGPOLL, Self::SIGIO);
    pub const SIGUNUSED: Self = Self::const_alias(bindings::LINUX_SIGUNUSED, Self::SIGSYS);

    pub fn is_realtime(&self) -> bool {
        (i32::from(Self::SIGRT_MIN)..=i32::from(Self::SIGRT_MAX)).contains(&self.0)
    }

    pub const fn as_i32(&self) -> i32 {
        self.0
    }
}

bitflags::bitflags! {
    #[repr(transparent)]
    #[derive(Copy, Clone, Debug, Default)]
    pub struct SigActionFlags: u64 {
        const NOCLDSTOP = bindings::LINUX_SA_NOCLDSTOP as u64;
        const NOCLDWAIT = bindings::LINUX_SA_NOCLDWAIT as u64;
        const NODEFER = bindings::LINUX_SA_NODEFER as u64;
        const ONSTACK = bindings::LINUX_SA_ONSTACK as u64;
        const RESETHAND = bindings::LINUX_SA_RESETHAND as u64;
        const RESTART = bindings::LINUX_SA_RESTART as u64;
        const RESTORER = bindings::LINUX_SA_RESTORER as u64;
        const SIGINFO = bindings::LINUX_SA_SIGINFO as u64;
    }
}
// We can't derive this since the bitflags field uses an internal type.
unsafe impl VirtualAddressSpaceIndependent for SigActionFlags {}

#[allow(non_camel_case_types)]
pub type linux_siginfo_t = bindings::linux_siginfo_t;

#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct SigInfo(linux_siginfo_t);
// Contains pointers, but they are understood to not necessarily be valid in the
// current address space.
unsafe impl VirtualAddressSpaceIndependent for SigInfo {}
unsafe impl Send for SigInfo {}

impl SigInfo {
    /// The bindings end up with a couple extra outer layers of unions.
    /// The outermost only has a single member; the next one has a data
    /// field and a padding field.
    fn inner(&self) -> &bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
        // SAFETY: If `self` was initialized at all, this fields will be initialized.
        // Technically it's possible in safe Rust to initialize the padding field
        // and not the data field, but we don't do that in this module, and don't
        // expose the inner type directly to Rust code, so it shouldn't be possible.
        unsafe { &self.0.l__bindgen_anon_1.l__bindgen_anon_1 }
    }

    /// As for `inner`
    fn inner_mut(&mut self) -> &mut bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
        unsafe { &mut self.0.l__bindgen_anon_1.l__bindgen_anon_1 }
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// `lsi_signo`, `lsi_errno`, and `lsi_code` must be initialized.
    pub unsafe fn wrap_assume_initd(si: linux_siginfo_t) -> Self {
        Self(si)
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_ref`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// `lsi_signo`, `lsi_errno`, and `lsi_code` must be initialized.
    pub unsafe fn wrap_ref_assume_initd(si: &linux_siginfo_t) -> &Self {
        unsafe { &*(si as *const _ as *const Self) }
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_mut`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// `lsi_signo`, `lsi_errno`, and `lsi_code` must be initialized.
    pub unsafe fn wrap_mut_assume_initd(si: &mut linux_siginfo_t) -> &mut Self {
        unsafe { &mut *(si as *mut _ as *mut Self) }
    }

    /// Analogous to `bytemuck::TransparentWrapper::peel`.
    pub fn peel(si: Self) -> linux_siginfo_t {
        si.0
    }

    #[inline]
    pub fn signo(&self) -> &i32 {
        &self.inner().lsi_signo
    }

    #[inline]
    pub fn signo_mut(&mut self) -> &mut i32 {
        &mut self.inner_mut().lsi_signo
    }

    #[inline]
    pub fn errno(&self) -> &i32 {
        &self.inner().lsi_errno
    }

    #[inline]
    pub fn errno_mut(&mut self) -> &mut i32 {
        &mut self.inner_mut().lsi_errno
    }

    #[inline]
    pub fn code(&self) -> &i32 {
        &self.inner().lsi_code
    }

    #[inline]
    pub fn code_mut(&mut self) -> &mut i32 {
        &mut self.inner_mut().lsi_code
    }

    // TODO: We should replace these individual setters with constructors
    // that initialize a whole sub-union based on which signal the siginfo is for.
    #[inline]
    pub fn set_pid(&mut self, pid: i32) {
        // We delegate to our linux_siginfo helper, which works on the pointers,
        // being careful not to create references that may be unsound.
        // e.g. we don't currently enforce that the rest of the innermost union
        // containing pid is initialized.
        unsafe { export::linux_siginfo_set_pid(&mut self.0, pid) }
    }

    // TODO: We should replace these individual setters with constructors
    // that initialize a whole sub-union based on which signal the siginfo is for.
    #[inline]
    pub fn set_uid(&mut self, uid: u32) {
        // See `set_pid`
        unsafe { export::linux_siginfo_set_uid(&mut self.0, uid) }
    }

    // TODO: We should replace these individual setters with constructors
    // that initialize a whole sub-union based on which signal the siginfo is for.
    #[inline]
    pub fn set_overrun(&mut self, overrun: i32) {
        // Compiler requires `unsafe` here because of the union access. Is this
        // a bug?  I think the point of `addr_of_mut` is that the intermediate
        // fields aren't actually dereferenced.
        let overrun_ptr = unsafe {
            core::ptr::addr_of_mut!(
                self.0
                    .l__bindgen_anon_1
                    .l__bindgen_anon_1
                    .l_sifields
                    .l_timer
                    .l_overrun
            )
        };
        unsafe { overrun_ptr.write(overrun) };
    }

    /// # Safety
    ///
    /// The overrun field must be known to be initialized.
    #[inline]
    pub unsafe fn get_overrun(&self) -> i32 {
        // Compiler requires `unsafe` here because of the union access. Is this
        // a bug?  I think the point of `addr_of_mut` is that the intermediate
        // fields aren't actually dereferenced.
        let overrun_ptr = unsafe {
            core::ptr::addr_of!(
                self.0
                    .l__bindgen_anon_1
                    .l__bindgen_anon_1
                    .l_sifields
                    .l_timer
                    .l_overrun
            )
        };
        unsafe { *overrun_ptr }
    }

    #[inline]
    pub fn new(signal: Signal, errno: i32, code: i32) -> Self {
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: signal.into(),
                    lsi_errno: errno,
                    lsi_code: code,
                    l_sifields: unsafe { core::mem::zeroed() },
                },
            },
        })
    }

    #[inline]
    pub fn new_sigalrm(overrun: i32) -> Self {
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: Signal::SIGALRM.into(),
                    lsi_errno: 0,
                    lsi_code: bindings::LINUX_SI_TIMER,
                    l_sifields: bindings::linux__sifields {
                        l_timer: bindings::linux__sifields__bindgen_ty_2 {
                            l_tid: 0,
                            l_overrun: overrun,
                            l_sigval: unsafe { core::mem::zeroed() },
                            l_sys_private: 0,
                        },
                    },
                },
            },
        })
    }
}

impl Default for SigInfo {
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

#[allow(non_camel_case_types)]
pub type linux_sigset_t = bindings::linux_sigset_t;

/// Compatible with the Linux kernel's definition of sigset_t on x86_64.
///
/// This is analagous to, but typically smaller than, libc's sigset_t.
#[repr(transparent)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Default, VirtualAddressSpaceIndependent)]
pub struct SigSet(linux_sigset_t);
unsafe impl TransparentWrapper<linux_sigset_t> for SigSet {}

impl SigSet {
    pub const EMPTY: Self = Self(0);
    pub const FULL: Self = Self(0);

    #[allow(unused)]
    fn deref_c(c: &bindings::linux_sigset_t) -> &Self {
        unsafe { &*(c as *const _ as *const Self) }
    }

    fn deref_mut_c(c: &mut bindings::linux_sigset_t) -> &mut Self {
        unsafe { &mut *(c as *mut _ as *mut Self) }
    }

    pub fn has(&self, sig: Signal) -> bool {
        (*self & SigSet::from(sig)).0 != 0
    }

    pub fn lowest(&self) -> Option<Signal> {
        if self.0 == 0 {
            return None;
        }
        for i in i32::from(Signal::MIN)..=i32::from(Signal::MAX) {
            let s = Signal::try_from(i).unwrap();
            if self.has(s) {
                return Some(s);
            }
        }
        unreachable!();
    }

    pub fn is_empty(&self) -> bool {
        *self == SigSet::EMPTY
    }

    pub fn del(&mut self, sig: Signal) {
        *self &= !SigSet::from(sig);
    }

    pub fn add(&mut self, sig: Signal) {
        *self |= SigSet::from(sig);
    }
}

impl From<Signal> for SigSet {
    #[inline]
    fn from(value: Signal) -> Self {
        let value = i32::from(value);
        debug_assert!(value <= 64);
        Self(1 << (value - 1))
    }
}

#[test]
fn test_from_signal() {
    let sigset = SigSet::from(Signal::SIGABRT);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(!sigset.has(Signal::SIGSEGV));
    assert_ne!(sigset, SigSet::EMPTY);
}

impl core::ops::BitOr for SigSet {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

#[test]
fn test_bitor() {
    let sigset = SigSet::from(Signal::SIGABRT) | SigSet::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
}

impl core::ops::BitOrAssign for SigSet {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0
    }
}

#[test]
fn test_bitorassign() {
    let mut sigset = SigSet::from(Signal::SIGABRT);
    sigset |= SigSet::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
}

impl core::ops::BitAnd for SigSet {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
}

#[test]
fn test_bitand() {
    let lhs = SigSet::from(Signal::SIGABRT) | SigSet::from(Signal::SIGSEGV);
    let rhs = SigSet::from(Signal::SIGABRT) | SigSet::from(Signal::SIGALRM);
    let and = lhs & rhs;
    assert!(and.has(Signal::SIGABRT));
    assert!(!and.has(Signal::SIGSEGV));
    assert!(!and.has(Signal::SIGALRM));
}

impl core::ops::BitAndAssign for SigSet {
    fn bitand_assign(&mut self, rhs: Self) {
        self.0 &= rhs.0
    }
}

#[test]
fn test_bitand_assign() {
    let mut set = SigSet::from(Signal::SIGABRT) | SigSet::from(Signal::SIGSEGV);
    set &= SigSet::from(Signal::SIGABRT) | SigSet::from(Signal::SIGALRM);
    assert!(set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(!set.has(Signal::SIGALRM));
}

impl core::ops::Not for SigSet {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}

#[test]
fn test_not() {
    let set = SigSet::from(Signal::SIGABRT) | SigSet::from(Signal::SIGSEGV);
    let set = !set;
    assert!(!set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(set.has(Signal::SIGALRM));
}

pub type SignalHandlerFn = unsafe extern "C" fn(i32);
pub type SignalActionFn = unsafe extern "C" fn(i32, *mut SigInfo, *mut core::ffi::c_void);

pub enum SignalHandler {
    Handler(SignalHandlerFn),
    Action(SignalActionFn),
    SigIgn,
    SigDfl,
}

/// Expose for cbindgen APIs
#[allow(non_camel_case_types)]
pub type linux_sigaction = bindings::linux_sigaction;

/// Compatible with kernel's definition of `struct sigaction`. Different from
/// libc's in that `ksa_handler` and `ksa_sigaction` are explicitly in a union,
/// and that `ksa_mask` is the kernel's mask size (64 bits) vs libc's larger one
/// (~1000 bits for glibc).
///
/// We use the field prefix lsa_ to avoid conflicting with macros defined for
/// the corresponding field names in glibc.
#[derive(Copy, Clone)]
#[repr(C)]
pub struct SigAction(linux_sigaction);
unsafe impl VirtualAddressSpaceIndependent for SigAction {}
unsafe impl Send for SigAction {}
unsafe impl TransparentWrapper<linux_sigaction> for SigAction {}

impl SigAction {
    // Bindgen doesn't succesfully bind these constants; maybe because
    // the macros defining them cast them to pointers.
    //
    // Copied from linux's include/uapi/asm-generic/signal-defs.h.
    const SIG_DFL: usize = 0;
    const SIG_IGN: usize = 1;

    pub fn flags(&self) -> Option<SigActionFlags> {
        SigActionFlags::from_bits(self.0.lsa_flags)
    }

    pub fn flags_retain(&self) -> SigActionFlags {
        SigActionFlags::from_bits_retain(self.0.lsa_flags)
    }

    pub fn handler(&self) -> SignalHandler {
        let as_usize = self.0.lsa_handler.map(|f| f as usize).unwrap_or(0);
        if as_usize == Self::SIG_IGN {
            SignalHandler::SigIgn
        } else if as_usize == Self::SIG_DFL {
            SignalHandler::SigDfl
        } else if self.flags_retain().contains(SigActionFlags::SIGINFO) {
            SignalHandler::Action(unsafe { core::mem::transmute(self.0.lsa_handler.unwrap()) })
        } else {
            SignalHandler::Handler(self.0.lsa_handler.unwrap())
        }
    }
}

impl Default for SigAction {
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

// Corresponds to default actions documented in signal(7).
#[derive(Eq, PartialEq)]
#[repr(C)]
pub enum LinuxDefaultAction {
    TERM,
    IGN,
    CORE,
    STOP,
    CONT,
}

pub fn defaultaction(sig: Signal) -> LinuxDefaultAction {
    use LinuxDefaultAction as Action;
    match sig  {
        Signal::SIGCONT => Action::CONT,
        // aka SIGIOT
        Signal::SIGABRT
        | Signal::SIGBUS
        | Signal::SIGFPE
        | Signal::SIGILL
        | Signal::SIGQUIT
        | Signal::SIGSEGV
        | Signal::SIGSYS
        | Signal::SIGTRAP
        | Signal::SIGXCPU
        | Signal::SIGXFSZ => Action::CORE,
        // aka SIGCLD
        Signal::SIGCHLD
        | Signal::SIGURG
        | Signal::SIGWINCH => Action::IGN,
        Signal::SIGSTOP
        | Signal::SIGTSTP
        | Signal::SIGTTIN
        | Signal::SIGTTOU => Action::STOP,
        Signal::SIGALRM
        //| SIGEMT
        | Signal::SIGHUP
        | Signal::SIGINT
        // aka SIGPOLL
        | Signal::SIGIO
        | Signal::SIGKILL
        //| SIGLOST
        | Signal::SIGPIPE
        | Signal::SIGPROF
        | Signal::SIGPWR
        | Signal::SIGSTKFLT
        | Signal::SIGTERM
        | Signal::SIGUSR1
        | Signal::SIGUSR2
        | Signal::SIGVTALRM => Action::TERM,
        //  realtime
        other => {
            assert!(other.is_realtime());
            // signal(7):
            // > The default action for an unhandled real-time signal is to
            // > terminate the receiving process.
            Action::TERM
        },
    }
}

mod export {
    use crate::bindings::{linux_siginfo_t, linux_sigset_t};

    use super::*;

    pub type LinuxSigHandlerFn = unsafe extern "C" fn(i32);
    pub type LinuxSigActionFn =
        unsafe extern "C" fn(i32, *mut linux_siginfo_t, *mut core::ffi::c_void);

    #[no_mangle]
    pub extern "C" fn linux_signal_is_valid(signo: i32) -> bool {
        Signal::try_from(signo).is_ok()
    }

    #[no_mangle]
    pub extern "C" fn linux_signal_is_realtime(signo: i32) -> bool {
        let Ok(signal) = Signal::try_from(signo) else {
            return false;
        };
        signal.is_realtime()
    }

    #[no_mangle]
    pub extern "C" fn linux_sigemptyset() -> linux_sigset_t {
        SigSet::EMPTY.0
    }

    #[no_mangle]
    pub extern "C" fn linux_sigfullset() -> linux_sigset_t {
        SigSet::FULL.0
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaddset(set: *mut linux_sigset_t, signo: i32) {
        let set = SigSet::deref_mut_c(unsafe { set.as_mut().unwrap() });
        let signo = Signal::try_from(signo).unwrap();
        set.add(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigdelset(set: *mut linux_sigset_t, signo: i32) {
        let set = SigSet::deref_mut_c(unsafe { set.as_mut().unwrap() });
        let signo = Signal::try_from(signo).unwrap();
        set.del(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigismember(set: *const linux_sigset_t, signo: i32) -> bool {
        let set = SigSet::deref_c(unsafe { set.as_ref().unwrap() });
        set.has(signo.try_into().unwrap())
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigisemptyset(set: *const linux_sigset_t) -> bool {
        let set = SigSet::deref_c(unsafe { set.as_ref().unwrap() });
        set.is_empty()
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigorset(
        lhs: *const linux_sigset_t,
        rhs: *const linux_sigset_t,
    ) -> linux_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        SigSet(*lhs | *rhs).0
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigandset(
        lhs: *const linux_sigset_t,
        rhs: *const linux_sigset_t,
    ) -> linux_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        SigSet(*lhs & *rhs).0
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_signotset(set: *const linux_sigset_t) -> linux_sigset_t {
        let set = unsafe { set.as_ref().unwrap() };
        SigSet(!*set).0
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_siglowest(set: *const linux_sigset_t) -> i32 {
        let set = SigSet::deref_c(unsafe { set.as_ref().unwrap() });
        match set.lowest() {
            Some(s) => s.into(),
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn linux_defaultAction(signo: i32) -> LinuxDefaultAction {
        let sig = Signal::try_from(signo).unwrap();
        defaultaction(sig)
    }

    #[no_mangle]
    pub extern "C" fn linux_siginfo_new(
        lsi_signo: i32,
        lsi_errno: i32,
        lsi_code: i32,
    ) -> linux_siginfo_t {
        // TODO: Lift errno and code types.
        let signal = Signal::try_from(lsi_signo).unwrap();
        SigInfo::peel(SigInfo::new(signal, lsi_errno, lsi_code))
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_siginfo_set_pid(si: *mut linux_siginfo_t, pid: i32) {
        let pid_ptr = core::ptr::addr_of_mut!(
            (*si)
                .l__bindgen_anon_1
                .l__bindgen_anon_1
                .l_sifields
                .l_kill
                .l_pid
        );
        unsafe { pid_ptr.write(pid) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_siginfo_set_uid(si: *mut linux_siginfo_t, uid: u32) {
        let uid_ptr = core::ptr::addr_of_mut!(
            (*si)
                .l__bindgen_anon_1
                .l__bindgen_anon_1
                .l_sifields
                .l_kill
                .l_uid
        );
        unsafe { uid_ptr.write(uid) };
    }

    /// Returns the handler if there is one, else NULL.
    // Ideally we'd want to return Option<LinuxSigHandler> here, but cbindgen doesn't
    // decay it back to LinuxSigHandler.
    #[no_mangle]
    #[allow(clippy::transmute_null_to_fn)]
    pub unsafe extern "C" fn linux_sigaction_handler(
        sa: *const linux_sigaction,
    ) -> LinuxSigHandlerFn {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        match sa.handler() {
            SignalHandler::Handler(h) => h,
            _ => unsafe { core::mem::transmute(core::ptr::null::<core::ffi::c_void>()) },
        }
    }

    /// Returns the action if there is one, else NULL.
    // Ideally we'd want to return Option<LinuxSigHandler> here, but cbindgen doesn't
    // decay it back to LinuxSigHandler.
    #[no_mangle]
    #[allow(clippy::transmute_null_to_fn)]
    pub unsafe extern "C" fn linux_sigaction_action(
        sa: *const linux_sigaction,
    ) -> LinuxSigActionFn {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        match sa.handler() {
            SignalHandler::Action(h) =>
            // We need to transmute the function pointer here, to one that takes
            // the exported C types.
            unsafe { core::mem::transmute::<SignalActionFn, LinuxSigActionFn>(h) },
            _ => unsafe { core::mem::transmute(core::ptr::null::<core::ffi::c_void>()) },
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaction_is_ign(sa: *const linux_sigaction) -> bool {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        matches!(sa.handler(), SignalHandler::SigIgn)
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaction_is_dfl(sa: *const linux_sigaction) -> bool {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        matches!(sa.handler(), SignalHandler::SigDfl)
    }
}
