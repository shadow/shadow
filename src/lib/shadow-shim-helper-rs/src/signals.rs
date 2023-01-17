use nix::sys::signal::{self, Signal};
use vasi::VirtualAddressSpaceIndependent;

pub const SHD_STANDARD_SIGNAL_MAX_NO: i32 = 31;

/// Lowest and highest valid realtime signal, according to signal(7).  We don't
/// use libc's SIGRTMIN and SIGRTMAX directly since those may omit some signal
/// numbers that libc reserves for its internal use. We still need to handle
/// those signal numbers in Shadow.
pub const SHD_SIGRT_MIN: i32 = 32;
pub const SHD_SIGRT_MAX: i32 = 64;

/// Definition is sometimes missing in the userspace headers. We could include
/// the kernel signal header, but it has definitions that conflict with the
/// userspace headers.
pub const SS_AUTODISARM: i32 = 1 << 31;

/// Compatible with the Linux kernel's definition of sigset_t on x86_64.
///
/// This is analagous to, but typically smaller than, libc's sigset_t.
#[repr(C)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Default, VirtualAddressSpaceIndependent)]
pub struct shd_kernel_sigset_t {
    val: u64,
}

impl shd_kernel_sigset_t {
    pub const EMPTY: Self = Self { val: 0 };
    pub const FULL: Self = Self { val: !0 };

    pub fn has(&self, sig: Signal) -> bool {
        (*self & shd_kernel_sigset_t::from(sig)).val != 0
    }

    pub fn lowest(&self) -> Option<Signal> {
        if self.val == 0 {
            return None;
        }
        for i in 1..=SHD_SIGRT_MAX {
            let s = Signal::try_from(i).unwrap();
            if self.has(s) {
                return Some(s);
            }
        }
        unreachable!("");
    }

    pub fn is_empty(&self) -> bool {
        *self == shd_kernel_sigset_t::EMPTY
    }

    pub fn del(&mut self, sig: Signal) {
        *self &= !shd_kernel_sigset_t::from(sig);
    }

    pub fn add(&mut self, sig: Signal) {
        *self |= shd_kernel_sigset_t::from(sig);
    }
}

impl From<Signal> for shd_kernel_sigset_t {
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
    let sigset = shd_kernel_sigset_t::from(Signal::SIGABRT);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(!sigset.has(Signal::SIGSEGV));
    assert_ne!(sigset, shd_kernel_sigset_t::EMPTY);
}

impl std::ops::BitOr for shd_kernel_sigset_t {
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
        shd_kernel_sigset_t::from(Signal::SIGABRT) | shd_kernel_sigset_t::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
}

impl std::ops::BitOrAssign for shd_kernel_sigset_t {
    fn bitor_assign(&mut self, rhs: Self) {
        self.val |= rhs.val
    }
}

#[test]
fn test_bitorassign() {
    let mut sigset = shd_kernel_sigset_t::from(Signal::SIGABRT);
    sigset |= shd_kernel_sigset_t::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
}

impl std::ops::BitAnd for shd_kernel_sigset_t {
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
        shd_kernel_sigset_t::from(Signal::SIGABRT) | shd_kernel_sigset_t::from(Signal::SIGSEGV);
    let rhs =
        shd_kernel_sigset_t::from(Signal::SIGABRT) | shd_kernel_sigset_t::from(Signal::SIGALRM);
    let and = lhs & rhs;
    assert!(and.has(Signal::SIGABRT));
    assert!(!and.has(Signal::SIGSEGV));
    assert!(!and.has(Signal::SIGALRM));
}

impl std::ops::BitAndAssign for shd_kernel_sigset_t {
    fn bitand_assign(&mut self, rhs: Self) {
        self.val &= rhs.val
    }
}

#[test]
fn test_bitand_assign() {
    let mut set =
        shd_kernel_sigset_t::from(Signal::SIGABRT) | shd_kernel_sigset_t::from(Signal::SIGSEGV);
    set &= shd_kernel_sigset_t::from(Signal::SIGABRT) | shd_kernel_sigset_t::from(Signal::SIGALRM);
    assert!(set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(!set.has(Signal::SIGALRM));
}

impl std::ops::Not for shd_kernel_sigset_t {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self { val: !self.val }
    }
}

#[test]
fn test_not() {
    let set =
        shd_kernel_sigset_t::from(Signal::SIGABRT) | shd_kernel_sigset_t::from(Signal::SIGSEGV);
    let set = !set;
    assert!(!set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(set.has(Signal::SIGALRM));
}

/// In C this is conventionally an anonymous union, but those aren't supported
/// in Rust. <https://github.com/rust-lang/rust/issues/49804>
#[repr(C)]
#[derive(Copy, Clone)]
pub union ShdKernelSigactionUnion {
    // Rust guarantees that the outer Option doesn't change the size:
    // https://doc.rust-lang.org/std/option/index.html#representation
    ksa_handler: Option<extern "C" fn(i32)>,
    ksa_sigaction: Option<extern "C" fn(i32, *mut libc::siginfo_t, *mut libc::c_void)>,
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
pub struct shd_kernel_sigaction {
    // SAFETY: We do not dereference the pointers in this union, except from the
    // shim, where it is valid to do so.
    #[unsafe_assume_virtual_address_space_independent]
    u: ShdKernelSigactionUnion,
    ksa_flags: i32,
    // Rust guarantees that the outer Option doesn't change the size:
    // https://doc.rust-lang.org/std/option/index.html#representation
    //
    // SAFETY: We never dereference this field.
    #[unsafe_assume_virtual_address_space_independent]
    ksa_restorer: Option<extern "C" fn()>,
    ksa_mask: shd_kernel_sigset_t,
}

impl shd_kernel_sigaction {
    pub fn handler(&self) -> signal::SigHandler {
        let handler_int: usize = unsafe { self.u.ksa_handler }
            .map(|f| f as usize)
            .unwrap_or(0);
        if handler_int == libc::SIG_IGN {
            signal::SigHandler::SigIgn
        } else if handler_int == libc::SIG_DFL {
            signal::SigHandler::SigDfl
        } else if self.ksa_flags & libc::SA_SIGINFO != 0 {
            signal::SigHandler::SigAction(unsafe { self.u.ksa_sigaction.unwrap() })
        } else {
            signal::SigHandler::Handler(unsafe { self.u.ksa_handler.unwrap() })
        }
    }
}

impl Default for shd_kernel_sigaction {
    fn default() -> Self {
        Self {
            u: ShdKernelSigactionUnion { ksa_handler: None },
            ksa_flags: Default::default(),
            ksa_restorer: Default::default(),
            ksa_mask: Default::default(),
        }
    }
}

// Corresponds to default actions documented in signal(7).
#[derive(Eq, PartialEq)]
#[repr(C)]
pub enum ShdKernelDefaultAction {
    TERM,
    IGN,
    CORE,
    STOP,
    CONT,
}

pub fn defaultaction(sig: Signal) -> ShdKernelDefaultAction {
    use ShdKernelDefaultAction as Action;
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
    pub extern "C" fn shd_sigemptyset() -> shd_kernel_sigset_t {
        shd_kernel_sigset_t::EMPTY
    }

    #[no_mangle]
    pub extern "C" fn shd_sigfullset() -> shd_kernel_sigset_t {
        shd_kernel_sigset_t::FULL
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_sigaddset(set: *mut shd_kernel_sigset_t, signo: i32) {
        let set = unsafe { set.as_mut().unwrap() };
        let signo = Signal::try_from(signo).unwrap();
        set.add(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_sigdelset(set: *mut shd_kernel_sigset_t, signo: i32) {
        let set = unsafe { set.as_mut().unwrap() };
        let signo = Signal::try_from(signo).unwrap();
        set.del(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_sigismember(set: *const shd_kernel_sigset_t, signo: i32) -> bool {
        let set = unsafe { set.as_ref().unwrap() };
        set.has(signo.try_into().unwrap())
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_sigisemptyset(set: *const shd_kernel_sigset_t) -> bool {
        let set = unsafe { set.as_ref().unwrap() };
        set.is_empty()
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_sigorset(
        lhs: *const shd_kernel_sigset_t,
        rhs: *const shd_kernel_sigset_t,
    ) -> shd_kernel_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        *lhs | *rhs
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_sigandset(
        lhs: *const shd_kernel_sigset_t,
        rhs: *const shd_kernel_sigset_t,
    ) -> shd_kernel_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        *lhs & *rhs
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_signotset(set: *const shd_kernel_sigset_t) -> shd_kernel_sigset_t {
        let set = unsafe { set.as_ref().unwrap() };
        !*set
    }

    #[no_mangle]
    pub unsafe extern "C" fn shd_siglowest(set: *const shd_kernel_sigset_t) -> i32 {
        let set = unsafe { set.as_ref().unwrap() };
        match set.lowest() {
            Some(s) => s as i32,
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn shd_defaultAction(signo: i32) -> ShdKernelDefaultAction {
        let sig = Signal::try_from(signo).unwrap();
        defaultaction(sig)
    }
}
