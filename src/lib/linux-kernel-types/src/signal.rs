use core::convert::TryFrom;
use num_enum::IntoPrimitive;
use num_enum::TryFromPrimitive;

use crate::bindings;
use crate::constants_bindings;

// signal names
#[derive(Debug, Copy, Clone, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
pub enum Signal {
    SIGHUP = constants_bindings::SIGHUP as i32,
    SIGINT = constants_bindings::SIGINT as i32,
    SIGQUIT = constants_bindings::SIGQUIT as i32,
    SIGILL = constants_bindings::SIGILL as i32,
    SIGTRAP = constants_bindings::SIGTRAP as i32,
    SIGABRT = constants_bindings::SIGABRT as i32,
    SIGBUS = constants_bindings::SIGBUS as i32,
    SIGFPE = constants_bindings::SIGFPE as i32,
    SIGKILL = constants_bindings::SIGKILL as i32,
    SIGUSR1 = constants_bindings::SIGUSR1 as i32,
    SIGSEGV = constants_bindings::SIGSEGV as i32,
    SIGUSR2 = constants_bindings::SIGUSR2 as i32,
    SIGPIPE = constants_bindings::SIGPIPE as i32,
    SIGALRM = constants_bindings::SIGALRM as i32,
    SIGTERM = constants_bindings::SIGTERM as i32,
    SIGSTKFLT = constants_bindings::SIGSTKFLT as i32,
    SIGCHLD = constants_bindings::SIGCHLD as i32,
    SIGCONT = constants_bindings::SIGCONT as i32,
    SIGSTOP = constants_bindings::SIGSTOP as i32,
    SIGTSTP = constants_bindings::SIGTSTP as i32,
    SIGTTIN = constants_bindings::SIGTTIN as i32,
    SIGTTOU = constants_bindings::SIGTTOU as i32,
    SIGURG = constants_bindings::SIGURG as i32,
    SIGXCPU = constants_bindings::SIGXCPU as i32,
    SIGXFSZ = constants_bindings::SIGXFSZ as i32,
    SIGVTALRM = constants_bindings::SIGVTALRM as i32,
    SIGPROF = constants_bindings::SIGPROF as i32,
    SIGWINCH = constants_bindings::SIGWINCH as i32,
    SIGIO = constants_bindings::SIGIO as i32,
    SIGPWR = constants_bindings::SIGPWR as i32,
    SIGSYS = constants_bindings::SIGSYS as i32,
}

impl Signal {
    const fn const_alias(from: u32, to: Self) -> Self {
        if to as i32 != from as i32 {
            // Can't use a format string here since this function is `const`
            panic!("Incorrect alias")
        }
        to
    }
    pub const SIGIOT: Self = Self::const_alias(constants_bindings::SIGIOT, Self::SIGABRT);
    pub const SIGPOLL: Self = Self::const_alias(constants_bindings::SIGPOLL, Self::SIGIO);
    pub const SIGUNUSED: Self = Self::const_alias(constants_bindings::SIGUNUSED, Self::SIGSYS);

    // We use i32 here instead of Self since the enum doesn't include
    // realtime signals. (But maybe it should?)
    pub const SIGRTMIN: i32 = constants_bindings::SIGRTMIN as i32;
    // XXX bindgen doesn't successfully import this one, so hard code.
    pub const SIGRTMAX: i32 = 64;
}

#[derive(Copy, Clone)]
#[allow(non_camel_case_types)]
pub struct siginfo_t(bindings::siginfo_t);

impl siginfo_t {
    pub fn signo(&self) -> i32 {
        // XXX safety
        unsafe { self.0.__bindgen_anon_1.__bindgen_anon_1.si_signo }
    }
    pub fn signo_set(&mut self, val: i32) {
        // XXX safety
        self.0.__bindgen_anon_1.__bindgen_anon_1.si_signo = val
    }

    pub fn errno(&self) -> i32 {
        // XXX safety
        unsafe { self.0.__bindgen_anon_1.__bindgen_anon_1.si_errno }
    }
    pub fn errno_set(&mut self, val: i32) {
        // XXX safety
        self.0.__bindgen_anon_1.__bindgen_anon_1.si_errno = val
    }

    pub fn code(&self) -> i32 {
        // XXX safety
        unsafe { self.0.__bindgen_anon_1.__bindgen_anon_1.si_code }
    }
    pub fn code_set(&mut self, val: i32) {
        // XXX safety
        self.0.__bindgen_anon_1.__bindgen_anon_1.si_code = val
    }
}

pub type SigActionHandlerHandler = unsafe extern "C" fn(i32);
pub type SigActionHandlerAction = unsafe extern "C" fn(i32, *mut siginfo_t, *mut core::ffi::c_void);

#[repr(C)]
pub union SigActionHandler {
    pub handler: SigActionHandlerHandler,
    pub action: SigActionHandlerAction,
    pub special: i64,
}

impl SigActionHandler {
    // bindgen doesn't handle these :(.
    // Copied from /usr/include/asm-generic/signal-defs.h
    pub const SIG_IGN: Self = Self { special: 1 };
    pub const SIG_DFL: Self = Self { special: 0 };
    pub const SIG_ERR: Self = Self { special: -1 };
}

pub type SigActionAction = unsafe extern "C" fn(i32, *mut siginfo_t, *mut core::ffi::c_void);

#[derive(Copy, Clone, Debug)]
#[repr(transparent)]
#[allow(non_camel_case_types)]
pub struct sigaction(bindings::sigaction);

impl sigaction {
    pub fn handler(&self) -> SigActionHandler {
        match self.0.sa_handler {
            Some(h) => unsafe { core::mem::transmute(h) },
            None => {
                let res = SigActionHandler::SIG_DFL;
                debug_assert_eq!(unsafe { res.special }, 0);
                res
            }
        }
    }
    pub fn handler_set(&mut self, val: SigActionHandler) {
        self.0.sa_handler = unsafe { core::mem::transmute(val) };
    }
}

impl Default for sigaction {
    fn default() -> Self {
        Self(bindings::sigaction {
            sa_handler: Default::default(),
            sa_mask: Default::default(),
            sa_restorer: Default::default(),
            sa_flags: Default::default(),
        })
    }
}

// Corresponds to default actions documented in signal(7).
#[derive(Eq, PartialEq)]
#[repr(C)]
pub enum DefaultAction {
    TERM,
    IGN,
    CORE,
    STOP,
    CONT,
}

pub fn defaultaction(sig: Signal) -> DefaultAction {
    use DefaultAction as Action;
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
    }
}

#[repr(transparent)]
#[allow(non_camel_case_types)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Default)]
pub struct sigset_t(bindings::sigset_t);
impl sigset_t {
    pub const EMPTY: Self = Self(0);
    pub const FULL: Self = Self(!0);

    pub fn has(&self, sig: Signal) -> bool {
        self.0 & Self::from(sig).0 != 0
    }

    pub fn lowest(&self) -> Option<Signal> {
        if self.0 == 0 {
            return None;
        }
        for i in 1..=Signal::SIGRTMAX {
            let s = Signal::try_from(i).unwrap();
            if self.has(s) {
                return Some(s);
            }
        }
        unreachable!("");
    }

    pub fn is_empty(&self) -> bool {
        *self == Self::EMPTY
    }

    pub fn del(&mut self, sig: Signal) {
        self.0 &= !Self::from(sig).0;
    }

    pub fn add(&mut self, sig: Signal) {
        self.0 |= Self::from(sig).0;
    }
}

impl From<Signal> for sigset_t {
    fn from(value: Signal) -> Self {
        let value = i32::from(value);
        debug_assert!(value >= 0);
        debug_assert!(value <= 64);
        Self(1 << (value - 1))
    }
}

impl core::ops::BitOr for sigset_t {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl core::ops::BitOrAssign for sigset_t {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0
    }
}

impl core::ops::BitAnd for sigset_t {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
}

impl core::ops::BitAndAssign for sigset_t {
    fn bitand_assign(&mut self, rhs: Self) {
        self.0 &= rhs.0
    }
}

impl core::ops::Not for sigset_t {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}

#[cfg(test)]
mod test_sigset {
    use super::*;

    #[test]
    fn sigset_size() {
        // The kernel definition should (currently) be 8 bytes.
        // At some point this may get increased, but it shouldn't be the glibc
        // size of ~100 bytes.
        assert_eq!(std::mem::size_of::<sigset_t>(), 8);
    }

    #[test]
    fn test_bitor() {
        let sigset = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGSEGV);
        assert!(sigset.has(Signal::SIGABRT));
        assert!(sigset.has(Signal::SIGSEGV));
        assert!(!sigset.has(Signal::SIGALRM));
    }

    #[test]
    fn test_bitorassign() {
        let mut sigset = sigset_t::from(Signal::SIGABRT);
        sigset |= sigset_t::from(Signal::SIGSEGV);
        assert!(sigset.has(Signal::SIGABRT));
        assert!(sigset.has(Signal::SIGSEGV));
        assert!(!sigset.has(Signal::SIGALRM));
    }

    #[test]
    fn test_bitand() {
        let lhs = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGSEGV);
        let rhs = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGALRM);
        let and = lhs & rhs;
        assert!(and.has(Signal::SIGABRT));
        assert!(!and.has(Signal::SIGSEGV));
        assert!(!and.has(Signal::SIGALRM));
    }

    #[test]
    fn test_bitand_assign() {
        let mut set = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGSEGV);
        set &= sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGALRM);
        assert!(set.has(Signal::SIGABRT));
        assert!(!set.has(Signal::SIGSEGV));
        assert!(!set.has(Signal::SIGALRM));
    }

    #[test]
    fn test_not() {
        let set = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGSEGV);
        let set = !set;
        assert!(!set.has(Signal::SIGABRT));
        assert!(!set.has(Signal::SIGSEGV));
        assert!(set.has(Signal::SIGALRM));
    }
}
