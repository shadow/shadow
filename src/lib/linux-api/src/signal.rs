use nix::sys::signal::Signal;
use vasi::VirtualAddressSpaceIndependent;

use crate::{bindings, constants_bindings};

pub const LINUX_STANDARD_SIGNAL_MAX_NO: i32 = 31;

/// Lowest and highest valid realtime signal, according to signal(7).  We don't
/// use libc's SIGRTMIN and SIGRTMAX directly since those may omit some signal
/// numbers that libc reserves for its internal use. We still need to handle
/// those signal numbers in Shadow.
pub const LINUX_SIGRT_MIN: i32 = 32;
pub const LINUX_SIGRT_MAX: i32 = 64;

// Definition is sometimes missing in the userspace headers. We could include
// the kernel signal header, but it has definitions that conflict with the
// userspace headers.
pub const LINUX_SS_AUTODISARM: i32 = 1 << 31;

// Bindgen doesn't succesfully bind these constants; maybe because
// the macros defining them cast them to pointers.
//
// Copied from linux's include/uapi/asm-generic/signal-defs.h.
pub const LINUX_SIG_DFL: usize = 0;
pub const LINUX_SIG_IGN: usize = 1;
pub const LINUX_SIG_ERR: usize = (-1_isize) as usize;

bitflags::bitflags! {
    #[repr(transparent)]
    #[derive(Copy, Clone, Debug, Default)]
    pub struct LinuxSigActionFlags: core::ffi::c_int {
        const NOCLDSTOP = constants_bindings::SA_NOCLDSTOP as i32;
        const NOCLDWAIT = constants_bindings::SA_NOCLDWAIT as i32;
        const NODEFER = constants_bindings::SA_NODEFER as i32;
        const ONSTACK = constants_bindings::SA_ONSTACK as i32;
        const RESETHAND = constants_bindings::SA_RESETHAND as i32;
        const RESTART = constants_bindings::SA_RESTART as i32;
        const RESTORER = constants_bindings::SA_RESTORER as i32;
        const SIGINFO = constants_bindings::SA_SIGINFO as i32;
    }
}
// We can't derive this since the bitflags field uses an internal type.
unsafe impl VirtualAddressSpaceIndependent for LinuxSigActionFlags {}

#[derive(Copy, Clone, VirtualAddressSpaceIndependent)]
#[allow(non_camel_case_types)]
#[repr(C)]
pub struct linux_siginfo_t {
    // We don't wrap bindings::siginfo_t directly,
    // since this would introduce a dependency on the original system
    // header file in the cbindgen'd version of this type, and requiring our C code
    // to include the kernel headers results in naming conflicts.

    // cbindgen doesn't understand repr(C, align(x)).
    // alignment validated by static assertion below.
    _align: u64,
    // We also can't use core::mem::size_of::<bindings::siginfo_t> to specify
    // the size here, because that also confuses cbindgen.
    // Size validated by static assertion below.
    _padding: [u8; 120],
}
static_assertions::assert_eq_align!(linux_siginfo_t, bindings::siginfo_t);
static_assertions::assert_eq_size!(linux_siginfo_t, bindings::siginfo_t);

impl linux_siginfo_t {
    fn as_bound_type(&self) -> &bindings::siginfo_t {
        static_assertions::assert_eq_align!(linux_siginfo_t, bindings::siginfo_t);
        static_assertions::assert_eq_size!(linux_siginfo_t, bindings::siginfo_t);
        unsafe { core::mem::transmute(self) }
    }

    fn as_bound_type_mut(&mut self) -> &mut bindings::siginfo_t {
        static_assertions::assert_eq_align!(linux_siginfo_t, bindings::siginfo_t);
        static_assertions::assert_eq_size!(linux_siginfo_t, bindings::siginfo_t);
        unsafe { core::mem::transmute(self) }
    }

    pub fn signo(&self) -> &i32 {
        unsafe {
            &self
                .as_bound_type()
                .__bindgen_anon_1
                .__bindgen_anon_1
                .si_signo
        }
    }

    pub fn signo_mut(&mut self) -> &mut i32 {
        unsafe {
            &mut self
                .as_bound_type_mut()
                .__bindgen_anon_1
                .__bindgen_anon_1
                .si_signo
        }
    }

    pub fn errno(&self) -> &i32 {
        unsafe {
            &self
                .as_bound_type()
                .__bindgen_anon_1
                .__bindgen_anon_1
                .si_errno
        }
    }

    pub fn errno_mut(&mut self) -> &mut i32 {
        unsafe {
            &mut self
                .as_bound_type_mut()
                .__bindgen_anon_1
                .__bindgen_anon_1
                .si_errno
        }
    }

    pub fn code(&self) -> &i32 {
        unsafe {
            &self
                .as_bound_type()
                .__bindgen_anon_1
                .__bindgen_anon_1
                .si_code
        }
    }

    pub fn code_mut(&mut self) -> &mut i32 {
        unsafe {
            &mut self
                .as_bound_type_mut()
                .__bindgen_anon_1
                .__bindgen_anon_1
                .si_code
        }
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

impl linux_sigset_t {
    pub const EMPTY: Self = Self { val: 0 };
    pub const FULL: Self = Self { val: !0 };

    pub fn has(&self, sig: Signal) -> bool {
        (*self & linux_sigset_t::from(sig)).val != 0
    }

    pub fn lowest(&self) -> Option<Signal> {
        if self.val == 0 {
            return None;
        }
        for i in 1..=LINUX_SIGRT_MAX {
            let s = Signal::try_from(i).unwrap();
            if self.has(s) {
                return Some(s);
            }
        }
        unreachable!("");
    }

    pub fn is_empty(&self) -> bool {
        *self == linux_sigset_t::EMPTY
    }

    pub fn del(&mut self, sig: Signal) {
        *self &= !linux_sigset_t::from(sig);
    }

    pub fn add(&mut self, sig: Signal) {
        *self |= linux_sigset_t::from(sig);
    }
}

impl From<Signal> for linux_sigset_t {
    fn from(value: Signal) -> Self {
        let value = value as i32;
        debug_assert!(value <= 64);
        Self {
            val: 1 << (value - 1),
        }
    }
}

#[test]
fn test_from_signal() {
    let sigset = linux_sigset_t::from(Signal::SIGABRT);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(!sigset.has(Signal::SIGSEGV));
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
    let sigset = linux_sigset_t::from(Signal::SIGABRT) | linux_sigset_t::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
}

impl core::ops::BitOrAssign for linux_sigset_t {
    fn bitor_assign(&mut self, rhs: Self) {
        self.val |= rhs.val
    }
}

#[test]
fn test_bitorassign() {
    let mut sigset = linux_sigset_t::from(Signal::SIGABRT);
    sigset |= linux_sigset_t::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
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
    let lhs = linux_sigset_t::from(Signal::SIGABRT) | linux_sigset_t::from(Signal::SIGSEGV);
    let rhs = linux_sigset_t::from(Signal::SIGABRT) | linux_sigset_t::from(Signal::SIGALRM);
    let and = lhs & rhs;
    assert!(and.has(Signal::SIGABRT));
    assert!(!and.has(Signal::SIGSEGV));
    assert!(!and.has(Signal::SIGALRM));
}

impl core::ops::BitAndAssign for linux_sigset_t {
    fn bitand_assign(&mut self, rhs: Self) {
        self.val &= rhs.val
    }
}

#[test]
fn test_bitand_assign() {
    let mut set = linux_sigset_t::from(Signal::SIGABRT) | linux_sigset_t::from(Signal::SIGSEGV);
    set &= linux_sigset_t::from(Signal::SIGABRT) | linux_sigset_t::from(Signal::SIGALRM);
    assert!(set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(!set.has(Signal::SIGALRM));
}

impl core::ops::Not for linux_sigset_t {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self { val: !self.val }
    }
}

#[test]
fn test_not() {
    let set = linux_sigset_t::from(Signal::SIGABRT) | linux_sigset_t::from(Signal::SIGSEGV);
    let set = !set;
    assert!(!set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(set.has(Signal::SIGALRM));
}

/// In C this is conventionally an anonymous union, but those aren't supported
/// in Rust. <https://github.com/rust-lang/rust/issues/49804>
#[repr(C)]
#[derive(Copy, Clone)]
pub union LinuxSigactionUnion {
    // Rust guarantees that the outer Option doesn't change the size:
    // https://doc.rust-lang.org/std/option/index.html#representation
    ksa_handler: Option<extern "C" fn(i32)>,
    ksa_sigaction: Option<extern "C" fn(i32, *mut linux_siginfo_t, *mut core::ffi::c_void)>,
}

/// Compatible with kernel's definition of `struct sigaction`. Different from
/// libc's in that `ksa_handler` and `ksa_sigaction` are explicitly in a union,
/// and that `ksa_mask` is the kernel's mask size (64 bits) vs libc's larger one
/// (~1000 bits for glibc).
///
/// We use the field prefix ksa_ to avoid conflicting with macros defined for
/// the corresponding field names in glibc.
#[derive(VirtualAddressSpaceIndependent, Copy, Clone)]
#[repr(C)]
pub struct linux_sigaction {
    // SAFETY: We do not dereference the pointers in this union, except from the
    // shim, where it is valid to do so.
    #[unsafe_assume_virtual_address_space_independent]
    u: LinuxSigactionUnion,
    ksa_flags: LinuxSigActionFlags,
    // Rust guarantees that the outer Option doesn't change the size:
    // https://doc.rust-lang.org/std/option/index.html#representation
    //
    // SAFETY: We never dereference this field.
    #[unsafe_assume_virtual_address_space_independent]
    ksa_restorer: Option<extern "C" fn()>,
    ksa_mask: linux_sigset_t,
}

impl linux_sigaction {
    pub fn handler(&self) -> nix::sys::signal::SigHandler {
        let handler_int: usize = unsafe { self.u.ksa_handler }
            .map(|f| f as usize)
            .unwrap_or(0);
        if handler_int == LINUX_SIG_IGN {
            nix::sys::signal::SigHandler::SigIgn
        } else if handler_int == LINUX_SIG_DFL {
            nix::sys::signal::SigHandler::SigDfl
        } else if self.ksa_flags.contains(LinuxSigActionFlags::SIGINFO) {
            // We need to cast the function pointer to what nix expects.
            // To avoid naming and depending on libc we use a transmute.
            // FIXME: get rid of transmute (and nix deps altogether).
            nix::sys::signal::SigHandler::SigAction(unsafe {
                core::mem::transmute(self.u.ksa_sigaction.unwrap())
            })
        } else {
            nix::sys::signal::SigHandler::Handler(unsafe { self.u.ksa_handler.unwrap() })
        }
    }
}

impl Default for linux_sigaction {
    fn default() -> Self {
        Self {
            u: LinuxSigactionUnion { ksa_handler: None },
            ksa_flags: Default::default(),
            ksa_restorer: Default::default(),
            ksa_mask: Default::default(),
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

pub fn defaultaction(sig: Signal) -> LinuxDefaultAction {
    use LinuxDefaultAction as Action;
    use Signal::*;
    match sig  {
        SIGCONT => Action::CONT,
        // aka SIGIOT
        SIGABRT
        | SIGBUS
        | SIGFPE
        | SIGILL
        | SIGQUIT
        | SIGSEGV
        | SIGSYS
        | SIGTRAP
        | SIGXCPU
        | SIGXFSZ => Action::CORE,
        // aka SIGCLD
        SIGCHLD
        | SIGURG
        | SIGWINCH => Action::IGN,
        SIGSTOP
        | SIGTSTP
        | SIGTTIN
        | SIGTTOU => Action::STOP,
        SIGALRM
        //| SIGEMT
        | SIGHUP
        | SIGINT
        // aka SIGPOLL
        | SIGIO
        | SIGKILL
        //| SIGLOST
        | SIGPIPE
        | SIGPROF
        | SIGPWR
        | SIGSTKFLT
        | SIGTERM
        | SIGUSR1
        | SIGUSR2
        | SIGVTALRM => Action::TERM,
        _ => {
            log::error!("Unhandled signal {}", sig);
            Action::CORE
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
        let signo = Signal::try_from(signo).unwrap();
        set.add(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigdelset(set: *mut linux_sigset_t, signo: i32) {
        let set = unsafe { set.as_mut().unwrap() };
        let signo = Signal::try_from(signo).unwrap();
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
            Some(s) => s as i32,
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn linux_defaultAction(signo: i32) -> LinuxDefaultAction {
        let sig = Signal::try_from(signo).unwrap();
        defaultaction(sig)
    }
}
