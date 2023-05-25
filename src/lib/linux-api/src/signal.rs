use core::mem::MaybeUninit;

use vasi::VirtualAddressSpaceIndependent;

use crate::bindings;

pub const LINUX_STANDARD_SIGNAL_MAX_NO: i32 = 31;

/// Lowest realtime signal number.
// We defined as a constant and cross-validate so that it gets exported
// via cbindgen.
pub const LINUX_SIGRT_MIN: i32 = 32;
static_assertions::const_assert_eq!(LINUX_SIGRT_MIN, bindings::SIGRTMIN as i32);

/// Highest realtime signal number.
//
// According to signal(7). bindgen fails to bind this one.
pub const LINUX_SIGRT_MAX: i32 = 64;

/// Definition is sometimes missing in the userspace headers.
//
// bindgen fails to bind this one.
// Copied from linux's include/uapi/linux/signal.h.
pub const LINUX_SS_AUTODISARM: i32 = 1 << 31;

// Bindgen doesn't succesfully bind these constants; maybe because
// the macros defining them cast them to pointers.
//
// Copied from linux's include/uapi/asm-generic/signal-defs.h.
pub const LINUX_SIG_DFL: usize = 0;
pub const LINUX_SIG_IGN: usize = 1;
pub const LINUX_SIG_ERR: usize = (-1_isize) as usize;

// signal names
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[repr(transparent)]
pub struct LinuxSignal(i32);

#[derive(Debug, Copy, Clone)]
pub struct LinuxSignalFromI32Error(pub i32);

impl TryFrom<i32> for LinuxSignal {
    type Error = LinuxSignalFromI32Error;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        if (1..=LINUX_SIGRT_MAX).contains(&value) {
            Ok(Self(value))
        } else {
            Err(LinuxSignalFromI32Error(value))
        }
    }
}

impl From<LinuxSignal> for i32 {
    fn from(value: LinuxSignal) -> Self {
        value.0
    }
}

impl LinuxSignal {
    pub const SIGHUP: Self = Self(bindings::SIGHUP as i32);
    pub const SIGINT: Self = Self(bindings::SIGINT as i32);
    pub const SIGQUIT: Self = Self(bindings::SIGQUIT as i32);
    pub const SIGILL: Self = Self(bindings::SIGILL as i32);
    pub const SIGTRAP: Self = Self(bindings::SIGTRAP as i32);
    pub const SIGABRT: Self = Self(bindings::SIGABRT as i32);
    pub const SIGBUS: Self = Self(bindings::SIGBUS as i32);
    pub const SIGFPE: Self = Self(bindings::SIGFPE as i32);
    pub const SIGKILL: Self = Self(bindings::SIGKILL as i32);
    pub const SIGUSR1: Self = Self(bindings::SIGUSR1 as i32);
    pub const SIGSEGV: Self = Self(bindings::SIGSEGV as i32);
    pub const SIGUSR2: Self = Self(bindings::SIGUSR2 as i32);
    pub const SIGPIPE: Self = Self(bindings::SIGPIPE as i32);
    pub const SIGALRM: Self = Self(bindings::SIGALRM as i32);
    pub const SIGTERM: Self = Self(bindings::SIGTERM as i32);
    pub const SIGSTKFLT: Self = Self(bindings::SIGSTKFLT as i32);
    pub const SIGCHLD: Self = Self(bindings::SIGCHLD as i32);
    pub const SIGCONT: Self = Self(bindings::SIGCONT as i32);
    pub const SIGSTOP: Self = Self(bindings::SIGSTOP as i32);
    pub const SIGTSTP: Self = Self(bindings::SIGTSTP as i32);
    pub const SIGTTIN: Self = Self(bindings::SIGTTIN as i32);
    pub const SIGTTOU: Self = Self(bindings::SIGTTOU as i32);
    pub const SIGURG: Self = Self(bindings::SIGURG as i32);
    pub const SIGXCPU: Self = Self(bindings::SIGXCPU as i32);
    pub const SIGXFSZ: Self = Self(bindings::SIGXFSZ as i32);
    pub const SIGVTALRM: Self = Self(bindings::SIGVTALRM as i32);
    pub const SIGPROF: Self = Self(bindings::SIGPROF as i32);
    pub const SIGWINCH: Self = Self(bindings::SIGWINCH as i32);
    pub const SIGIO: Self = Self(bindings::SIGIO as i32);
    pub const SIGPWR: Self = Self(bindings::SIGPWR as i32);
    pub const SIGSYS: Self = Self(bindings::SIGSYS as i32);
    pub const SIGRT_MIN: Self = Self(LINUX_SIGRT_MIN);
    pub const SIGRT_MAX: Self = Self(LINUX_SIGRT_MAX);

    const fn const_alias(from: u32, to: Self) -> Self {
        if from as i32 != to.0 {
            // Can't use a format string here since this function is `const`
            panic!("Incorrect alias")
        }
        to
    }
    pub const SIGIOT: Self = Self::const_alias(bindings::SIGIOT, Self::SIGABRT);
    pub const SIGPOLL: Self = Self::const_alias(bindings::SIGPOLL, Self::SIGIO);
    pub const SIGUNUSED: Self = Self::const_alias(bindings::SIGUNUSED, Self::SIGSYS);

    pub fn is_realtime(&self) -> bool {
        (i32::from(Self::SIGRT_MIN)..=i32::from(Self::SIGRT_MAX)).contains(&self.0)
    }
}

bitflags::bitflags! {
    #[repr(transparent)]
    #[derive(Copy, Clone, Debug, Default)]
    pub struct LinuxSigActionFlags: core::ffi::c_int {
        const NOCLDSTOP = bindings::SA_NOCLDSTOP as i32;
        const NOCLDWAIT = bindings::SA_NOCLDWAIT as i32;
        const NODEFER = bindings::SA_NODEFER as i32;
        const ONSTACK = bindings::SA_ONSTACK as i32;
        const RESETHAND = bindings::SA_RESETHAND as i32;
        const RESTART = bindings::SA_RESTART as i32;
        const RESTORER = bindings::SA_RESTORER as i32;
        const SIGINFO = bindings::SA_SIGINFO as i32;
    }
}
// We can't derive this since the bitflags field uses an internal type.
unsafe impl VirtualAddressSpaceIndependent for LinuxSigActionFlags {}

#[derive(Copy, Clone, VirtualAddressSpaceIndependent)]
#[allow(non_camel_case_types)]
#[repr(C)]
pub struct linux_siginfo_t {
    // We have to prefix these field names to avoid conflicts with
    // macros in our exported C bindings.
    lsi_signo: i32,
    lsi_errno: i32,
    lsi_code: i32,

    // TODO: Consider defining the rest of the fields. It's a lot of nested
    // unions, so may not be worth fully defining here. Instead we can just
    // add accessor methods for the fields we need.

    // cbindgen doesn't understand repr(C, align(x)).
    // alignment validated by static assertion below.
    _align: u64,
    // We also can't use core::mem::size_of::<bindings::siginfo_t> to specify
    // the size here, because that also confuses cbindgen.
    // Size validated by static assertion below.
    _padding: MaybeUninit<[u8; 100]>,
}
static_assertions::assert_eq_align!(linux_siginfo_t, bindings::siginfo_t);
static_assertions::assert_eq_size!(linux_siginfo_t, bindings::siginfo_t);

#[cfg(test)]
#[test]
fn test_linux_siginfo_layout() {
    assert_eq!(
        memoffset::offset_of!(linux_siginfo_t, lsi_signo),
        memoffset::offset_of!(bindings::siginfo, __bindgen_anon_1)
            + memoffset::offset_of_union!(bindings::siginfo__bindgen_ty_1, __bindgen_anon_1)
            + memoffset::offset_of!(bindings::siginfo__bindgen_ty_1__bindgen_ty_1, si_signo)
    );
    assert_eq!(
        memoffset::offset_of!(linux_siginfo_t, lsi_errno),
        memoffset::offset_of!(bindings::siginfo, __bindgen_anon_1)
            + memoffset::offset_of_union!(bindings::siginfo__bindgen_ty_1, __bindgen_anon_1)
            + memoffset::offset_of!(bindings::siginfo__bindgen_ty_1__bindgen_ty_1, si_errno)
    );
    assert_eq!(
        memoffset::offset_of!(linux_siginfo_t, lsi_code),
        memoffset::offset_of!(bindings::siginfo, __bindgen_anon_1)
            + memoffset::offset_of_union!(bindings::siginfo__bindgen_ty_1, __bindgen_anon_1)
            + memoffset::offset_of!(bindings::siginfo__bindgen_ty_1__bindgen_ty_1, si_code)
    );
}

impl linux_siginfo_t {
    fn as_bound_type(&self) -> &bindings::siginfo_t {
        // SAFETY: Same layout. The `MaybeUninit` `_padding` section corresponds
        // to unions in the bound type, which still require `unsafe` to read.
        unsafe { &*(self as *const _ as *const bindings::siginfo_t) }
    }

    fn as_bound_type_mut(&mut self) -> &mut bindings::siginfo_t {
        // SAFETY: Same layout. The `MaybeUninit` `_padding` section corresponds
        // to unions in the bound type, which still require `unsafe` to read.
        unsafe { &mut *(self as *mut _ as *mut bindings::siginfo_t) }
    }

    #[inline]
    pub fn signo(&self) -> &i32 {
        &self.lsi_signo
    }

    #[inline]
    pub fn signo_mut(&mut self) -> &mut i32 {
        &mut self.lsi_signo
    }

    #[inline]
    pub fn errno(&self) -> &i32 {
        &self.lsi_errno
    }

    #[inline]
    pub fn errno_mut(&mut self) -> &mut i32 {
        &mut self.lsi_errno
    }

    #[inline]
    pub fn code(&self) -> &i32 {
        &self.lsi_code
    }

    #[inline]
    pub fn code_mut(&mut self) -> &mut i32 {
        &mut self.lsi_code
    }

    #[inline]
    pub fn set_pid(&mut self, pid: i32) {
        // union fields are always in the same position in the unions where they are defined.
        self.as_bound_type_mut()
            .__bindgen_anon_1
            .__bindgen_anon_1
            ._sifields
            ._kill
            ._pid = pid;
    }

    #[inline]
    pub fn set_uid(&mut self, uid: u32) {
        // union fields are always in the same position in the unions where they are defined.
        self.as_bound_type_mut()
            .__bindgen_anon_1
            .__bindgen_anon_1
            ._sifields
            ._rt
            ._uid = uid;
    }

    #[inline]
    pub fn set_overrun(&mut self, overrun: i32) {
        // union fields are always in the same position in the unions where they are defined.
        self.as_bound_type_mut()
            .__bindgen_anon_1
            .__bindgen_anon_1
            ._sifields
            ._timer
            ._overrun = overrun;
    }

    /// # Safety
    ///
    /// The overrun field must be known to be initialized.
    #[inline]
    pub unsafe fn get_overrun(&mut self) -> i32 {
        // union fields are always in the same position in the unions where they are defined.
        unsafe {
            self.as_bound_type()
                .__bindgen_anon_1
                .__bindgen_anon_1
                ._sifields
                ._timer
                ._overrun
        }
    }

    #[inline]
    pub fn new(signal: LinuxSignal, errno: i32, code: i32) -> Self {
        linux_siginfo_t {
            lsi_signo: signal.into(),
            lsi_errno: errno,
            lsi_code: code,
            _align: Default::default(),
            _padding: MaybeUninit::zeroed(),
        }
    }

    #[inline]
    pub fn new_sigalrm(overrun: i32) -> Self {
        let mut s = linux_siginfo_t {
            lsi_signo: LinuxSignal::SIGALRM.into(),
            lsi_errno: 0,
            lsi_code: bindings::SI_TIMER,
            _align: Default::default(),
            _padding: MaybeUninit::uninit(),
        };
        s.as_bound_type_mut()
            .__bindgen_anon_1
            .__bindgen_anon_1
            ._sifields
            ._timer
            ._overrun = overrun;
        s
    }
}

impl Default for linux_siginfo_t {
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

/// Compatible with the Linux kernel's definition of sigset_t on x86_64.
///
/// This is analagous to, but typically smaller than, libc's sigset_t.
#[repr(C)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Default, VirtualAddressSpaceIndependent)]
pub struct linux_sigset_t {
    val: u64,
}
static_assertions::assert_eq_align!(linux_sigset_t, bindings::sigset_t);
static_assertions::assert_eq_size!(linux_sigset_t, bindings::sigset_t);

impl linux_sigset_t {
    pub const EMPTY: Self = Self { val: 0 };
    pub const FULL: Self = Self { val: !0 };

    pub fn has(&self, sig: LinuxSignal) -> bool {
        (*self & linux_sigset_t::from(sig)).val != 0
    }

    pub fn lowest(&self) -> Option<LinuxSignal> {
        if self.val == 0 {
            return None;
        }
        for i in 1..=LINUX_SIGRT_MAX {
            let s = LinuxSignal::try_from(i).unwrap();
            if self.has(s) {
                return Some(s);
            }
        }
        unreachable!();
    }

    pub fn is_empty(&self) -> bool {
        *self == linux_sigset_t::EMPTY
    }

    pub fn del(&mut self, sig: LinuxSignal) {
        *self &= !linux_sigset_t::from(sig);
    }

    pub fn add(&mut self, sig: LinuxSignal) {
        *self |= linux_sigset_t::from(sig);
    }
}

impl From<LinuxSignal> for linux_sigset_t {
    fn from(value: LinuxSignal) -> Self {
        let value = i32::from(value);
        debug_assert!(value <= 64);
        Self {
            val: 1 << (value - 1),
        }
    }
}

#[test]
fn test_from_signal() {
    let sigset = linux_sigset_t::from(LinuxSignal::SIGABRT);
    assert!(sigset.has(LinuxSignal::SIGABRT));
    assert!(!sigset.has(LinuxSignal::SIGSEGV));
    assert_ne!(sigset, linux_sigset_t::EMPTY);
}

impl core::ops::BitOr for linux_sigset_t {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self {
            val: self.val | rhs.val,
        }
    }
}

#[test]
fn test_bitor() {
    let sigset =
        linux_sigset_t::from(LinuxSignal::SIGABRT) | linux_sigset_t::from(LinuxSignal::SIGSEGV);
    assert!(sigset.has(LinuxSignal::SIGABRT));
    assert!(sigset.has(LinuxSignal::SIGSEGV));
    assert!(!sigset.has(LinuxSignal::SIGALRM));
}

impl core::ops::BitOrAssign for linux_sigset_t {
    fn bitor_assign(&mut self, rhs: Self) {
        self.val |= rhs.val
    }
}

#[test]
fn test_bitorassign() {
    let mut sigset = linux_sigset_t::from(LinuxSignal::SIGABRT);
    sigset |= linux_sigset_t::from(LinuxSignal::SIGSEGV);
    assert!(sigset.has(LinuxSignal::SIGABRT));
    assert!(sigset.has(LinuxSignal::SIGSEGV));
    assert!(!sigset.has(LinuxSignal::SIGALRM));
}

impl core::ops::BitAnd for linux_sigset_t {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        Self {
            val: self.val & rhs.val,
        }
    }
}

#[test]
fn test_bitand() {
    let lhs =
        linux_sigset_t::from(LinuxSignal::SIGABRT) | linux_sigset_t::from(LinuxSignal::SIGSEGV);
    let rhs =
        linux_sigset_t::from(LinuxSignal::SIGABRT) | linux_sigset_t::from(LinuxSignal::SIGALRM);
    let and = lhs & rhs;
    assert!(and.has(LinuxSignal::SIGABRT));
    assert!(!and.has(LinuxSignal::SIGSEGV));
    assert!(!and.has(LinuxSignal::SIGALRM));
}

impl core::ops::BitAndAssign for linux_sigset_t {
    fn bitand_assign(&mut self, rhs: Self) {
        self.val &= rhs.val
    }
}

#[test]
fn test_bitand_assign() {
    let mut set =
        linux_sigset_t::from(LinuxSignal::SIGABRT) | linux_sigset_t::from(LinuxSignal::SIGSEGV);
    set &= linux_sigset_t::from(LinuxSignal::SIGABRT) | linux_sigset_t::from(LinuxSignal::SIGALRM);
    assert!(set.has(LinuxSignal::SIGABRT));
    assert!(!set.has(LinuxSignal::SIGSEGV));
    assert!(!set.has(LinuxSignal::SIGALRM));
}

impl core::ops::Not for linux_sigset_t {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self { val: !self.val }
    }
}

#[test]
fn test_not() {
    let set =
        linux_sigset_t::from(LinuxSignal::SIGABRT) | linux_sigset_t::from(LinuxSignal::SIGSEGV);
    let set = !set;
    assert!(!set.has(LinuxSignal::SIGABRT));
    assert!(!set.has(LinuxSignal::SIGSEGV));
    assert!(set.has(LinuxSignal::SIGALRM));
}

/// In C this is conventionally an anonymous union, but those aren't supported
/// in Rust. <https://github.com/rust-lang/rust/issues/49804>
#[repr(C)]
#[derive(Copy, Clone)]
pub union LinuxSignalHandler {
    // Rust guarantees that the outer Option doesn't change the size:
    // https://doc.rust-lang.org/std/option/index.html#representation
    lsa_handler: Option<unsafe extern "C" fn(i32)>,
    lsa_sigaction: Option<unsafe extern "C" fn(i32, *mut linux_siginfo_t, *mut core::ffi::c_void)>,
}

impl LinuxSignalHandler {
    fn as_usize(&self) -> usize {
        unsafe { self.lsa_handler }.map(|f| f as usize).unwrap_or(0)
    }

    pub fn is_sig_ign(&self) -> bool {
        self.as_usize() == LINUX_SIG_IGN
    }

    pub fn is_sig_dfl(&self) -> bool {
        self.as_usize() == LINUX_SIG_DFL
    }
}

/// Compatible with kernel's definition of `struct sigaction`. Different from
/// libc's in that `ksa_handler` and `ksa_sigaction` are explicitly in a union,
/// and that `ksa_mask` is the kernel's mask size (64 bits) vs libc's larger one
/// (~1000 bits for glibc).
///
/// We use the field prefix lsa_ to avoid conflicting with macros defined for
/// the corresponding field names in glibc.
#[derive(VirtualAddressSpaceIndependent, Copy, Clone)]
#[repr(C)]
pub struct linux_sigaction {
    // SAFETY: We do not dereference the pointers in this union, except from the
    // shim, where it is valid to do so.
    #[unsafe_assume_virtual_address_space_independent]
    pub u: LinuxSignalHandler,
    pub lsa_flags: LinuxSigActionFlags,
    // Rust guarantees that the outer Option doesn't change the size:
    // https://doc.rust-lang.org/std/option/index.html#representation
    //
    // SAFETY: We never dereference this field.
    #[unsafe_assume_virtual_address_space_independent]
    pub lsa_restorer: Option<extern "C" fn()>,
    pub lsa_mask: linux_sigset_t,
}
static_assertions::assert_eq_align!(linux_sigaction, bindings::sigaction);
static_assertions::assert_eq_size!(linux_sigaction, bindings::sigaction);

#[cfg(test)]
#[test]
fn test_linux_sigaction_layout() {
    assert_eq!(
        memoffset::offset_of!(linux_sigaction, u)
            + memoffset::offset_of_union!(LinuxSignalHandler, lsa_handler),
        memoffset::offset_of!(bindings::sigaction, sa_handler)
    );
    // Bindgen doesn't create an `sa_sigaction` field; both `sa_handler` and
    // `sa_sigaction` are stored directly in `sa_handler`.
    assert_eq!(
        memoffset::offset_of!(linux_sigaction, u)
            + memoffset::offset_of_union!(LinuxSignalHandler, lsa_sigaction),
        memoffset::offset_of!(bindings::sigaction, sa_handler)
    );
    assert_eq!(
        memoffset::offset_of!(linux_sigaction, lsa_flags),
        memoffset::offset_of!(bindings::sigaction, sa_flags)
    );
    assert_eq!(
        memoffset::offset_of!(linux_sigaction, lsa_restorer),
        memoffset::offset_of!(bindings::sigaction, sa_restorer)
    );
    assert_eq!(
        memoffset::offset_of!(linux_sigaction, lsa_mask),
        memoffset::offset_of!(bindings::sigaction, sa_mask)
    );
}

impl linux_sigaction {
    pub fn handler(&self) -> &LinuxSignalHandler {
        &self.u
    }
}

impl Default for linux_sigaction {
    fn default() -> Self {
        Self {
            u: LinuxSignalHandler { lsa_handler: None },
            lsa_flags: Default::default(),
            lsa_restorer: Default::default(),
            lsa_mask: Default::default(),
        }
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

pub fn defaultaction(sig: LinuxSignal) -> LinuxDefaultAction {
    use LinuxDefaultAction as Action;
    match sig  {
        LinuxSignal::SIGCONT => Action::CONT,
        // aka SIGIOT
        LinuxSignal::SIGABRT
        | LinuxSignal::SIGBUS
        | LinuxSignal::SIGFPE
        | LinuxSignal::SIGILL
        | LinuxSignal::SIGQUIT
        | LinuxSignal::SIGSEGV
        | LinuxSignal::SIGSYS
        | LinuxSignal::SIGTRAP
        | LinuxSignal::SIGXCPU
        | LinuxSignal::SIGXFSZ => Action::CORE,
        // aka SIGCLD
        LinuxSignal::SIGCHLD
        | LinuxSignal::SIGURG
        | LinuxSignal::SIGWINCH => Action::IGN,
        LinuxSignal::SIGSTOP
        | LinuxSignal::SIGTSTP
        | LinuxSignal::SIGTTIN
        | LinuxSignal::SIGTTOU => Action::STOP,
        LinuxSignal::SIGALRM
        //| SIGEMT
        | LinuxSignal::SIGHUP
        | LinuxSignal::SIGINT
        // aka SIGPOLL
        | LinuxSignal::SIGIO
        | LinuxSignal::SIGKILL
        //| SIGLOST
        | LinuxSignal::SIGPIPE
        | LinuxSignal::SIGPROF
        | LinuxSignal::SIGPWR
        | LinuxSignal::SIGSTKFLT
        | LinuxSignal::SIGTERM
        | LinuxSignal::SIGUSR1
        | LinuxSignal::SIGUSR2
        | LinuxSignal::SIGVTALRM => Action::TERM,
        //  realtime
        other => {
            assert!(other.is_realtime());
            Action::IGN
        },
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn linux_sigemptyset() -> linux_sigset_t {
        linux_sigset_t::EMPTY
    }

    #[no_mangle]
    pub extern "C" fn linux_sigfullset() -> linux_sigset_t {
        linux_sigset_t::FULL
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaddset(set: *mut linux_sigset_t, signo: i32) {
        let set = unsafe { set.as_mut().unwrap() };
        let signo = LinuxSignal::try_from(signo).unwrap();
        set.add(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigdelset(set: *mut linux_sigset_t, signo: i32) {
        let set = unsafe { set.as_mut().unwrap() };
        let signo = LinuxSignal::try_from(signo).unwrap();
        set.del(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigismember(set: *const linux_sigset_t, signo: i32) -> bool {
        let set = unsafe { set.as_ref().unwrap() };
        set.has(signo.try_into().unwrap())
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigisemptyset(set: *const linux_sigset_t) -> bool {
        let set = unsafe { set.as_ref().unwrap() };
        set.is_empty()
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigorset(
        lhs: *const linux_sigset_t,
        rhs: *const linux_sigset_t,
    ) -> linux_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        *lhs | *rhs
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigandset(
        lhs: *const linux_sigset_t,
        rhs: *const linux_sigset_t,
    ) -> linux_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        *lhs & *rhs
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_signotset(set: *const linux_sigset_t) -> linux_sigset_t {
        let set = unsafe { set.as_ref().unwrap() };
        !*set
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_siglowest(set: *const linux_sigset_t) -> i32 {
        let set = unsafe { set.as_ref().unwrap() };
        match set.lowest() {
            Some(s) => s.into(),
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn linux_defaultAction(signo: i32) -> LinuxDefaultAction {
        let sig = LinuxSignal::try_from(signo).unwrap();
        defaultaction(sig)
    }

    #[no_mangle]
    pub extern "C" fn linux_siginfo_new(
        lsi_signo: i32,
        lsi_errno: i32,
        lsi_code: i32,
    ) -> linux_siginfo_t {
        // TODO: Lift errno and code types.
        let signal = LinuxSignal::try_from(lsi_signo).unwrap();
        linux_siginfo_t::new(signal, lsi_errno, lsi_code)
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_siginfo_set_pid(si: *mut linux_siginfo_t, pid: i32) {
        let si = unsafe { si.as_mut().unwrap() };
        si.set_pid(pid)
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_siginfo_set_uid(si: *mut linux_siginfo_t, uid: u32) {
        let si = unsafe { si.as_mut().unwrap() };
        si.set_uid(uid)
    }
}
