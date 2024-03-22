use bytemuck::TransparentWrapper;
use linux_syscall::syscall;
use linux_syscall::Result as LinuxSyscallResult;
use num_enum::{IntoPrimitive, TryFromPrimitive};
use shadow_pod::Pod;
use vasi::VirtualAddressSpaceIndependent;

use crate::bindings::{self, linux_sigval};
use crate::const_conversions;
use crate::const_conversions::i32_from_u32_allowing_wraparound;
use crate::errno::Errno;
use crate::posix_types::kernel_pid_t;
use crate::posix_types::Pid;

// signal names. This is a `struct` instead of an `enum` to support
// realtime signals.
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
    pub const SIGHUP: Self = Self::std_from_u32_const(bindings::LINUX_SIGHUP);
    pub const SIGINT: Self = Self::std_from_u32_const(bindings::LINUX_SIGINT);
    pub const SIGQUIT: Self = Self::std_from_u32_const(bindings::LINUX_SIGQUIT);
    pub const SIGILL: Self = Self::std_from_u32_const(bindings::LINUX_SIGILL);
    pub const SIGTRAP: Self = Self::std_from_u32_const(bindings::LINUX_SIGTRAP);
    pub const SIGABRT: Self = Self::std_from_u32_const(bindings::LINUX_SIGABRT);
    pub const SIGBUS: Self = Self::std_from_u32_const(bindings::LINUX_SIGBUS);
    pub const SIGFPE: Self = Self::std_from_u32_const(bindings::LINUX_SIGFPE);
    pub const SIGKILL: Self = Self::std_from_u32_const(bindings::LINUX_SIGKILL);
    pub const SIGUSR1: Self = Self::std_from_u32_const(bindings::LINUX_SIGUSR1);
    pub const SIGSEGV: Self = Self::std_from_u32_const(bindings::LINUX_SIGSEGV);
    pub const SIGUSR2: Self = Self::std_from_u32_const(bindings::LINUX_SIGUSR2);
    pub const SIGPIPE: Self = Self::std_from_u32_const(bindings::LINUX_SIGPIPE);
    pub const SIGALRM: Self = Self::std_from_u32_const(bindings::LINUX_SIGALRM);
    pub const SIGTERM: Self = Self::std_from_u32_const(bindings::LINUX_SIGTERM);
    pub const SIGSTKFLT: Self = Self::std_from_u32_const(bindings::LINUX_SIGSTKFLT);
    pub const SIGCHLD: Self = Self::std_from_u32_const(bindings::LINUX_SIGCHLD);
    pub const SIGCONT: Self = Self::std_from_u32_const(bindings::LINUX_SIGCONT);
    pub const SIGSTOP: Self = Self::std_from_u32_const(bindings::LINUX_SIGSTOP);
    pub const SIGTSTP: Self = Self::std_from_u32_const(bindings::LINUX_SIGTSTP);
    pub const SIGTTIN: Self = Self::std_from_u32_const(bindings::LINUX_SIGTTIN);
    pub const SIGTTOU: Self = Self::std_from_u32_const(bindings::LINUX_SIGTTOU);
    pub const SIGURG: Self = Self::std_from_u32_const(bindings::LINUX_SIGURG);
    pub const SIGXCPU: Self = Self::std_from_u32_const(bindings::LINUX_SIGXCPU);
    pub const SIGXFSZ: Self = Self::std_from_u32_const(bindings::LINUX_SIGXFSZ);
    pub const SIGVTALRM: Self = Self::std_from_u32_const(bindings::LINUX_SIGVTALRM);
    pub const SIGPROF: Self = Self::std_from_u32_const(bindings::LINUX_SIGPROF);
    pub const SIGWINCH: Self = Self::std_from_u32_const(bindings::LINUX_SIGWINCH);
    pub const SIGIO: Self = Self::std_from_u32_const(bindings::LINUX_SIGIO);
    pub const SIGPWR: Self = Self::std_from_u32_const(bindings::LINUX_SIGPWR);
    pub const SIGSYS: Self = Self::std_from_u32_const(bindings::LINUX_SIGSYS);

    pub const STANDARD_MAX: Self = Self(31);

    pub const SIGRT_MIN: Self = Self::rt_from_u32_const(bindings::LINUX_SIGRTMIN);
    // According to signal(7). bindgen fails to bind this one.
    pub const SIGRT_MAX: Self = Self::rt_from_u32_const(64);

    pub const MIN: Self = Self(1);
    pub const MAX: Self = Self::SIGRT_MAX;

    // Aliases
    pub const SIGIOT: Self = Self::std_from_u32_const(bindings::LINUX_SIGIOT);
    pub const SIGPOLL: Self = Self::std_from_u32_const(bindings::LINUX_SIGPOLL);
    pub const SIGUNUSED: Self = Self::std_from_u32_const(bindings::LINUX_SIGUNUSED);

    pub fn is_realtime(&self) -> bool {
        (i32::from(Self::SIGRT_MIN)..=i32::from(Self::SIGRT_MAX)).contains(&self.0)
    }

    pub const fn as_i32(&self) -> i32 {
        self.0
    }

    pub fn standard_signals() -> impl Iterator<Item = Signal> {
        (i32::from(Self::MIN)..=i32::from(Self::STANDARD_MAX)).map(|i| Self::try_from(i).unwrap())
    }

    // Checked conversion from bindings
    const fn std_from_u32_const(val: u32) -> Self {
        let rv = Self(const_conversions::i32_from_u32(val));
        assert!(rv.0 as u32 == val);
        assert!(rv.0 <= Self::STANDARD_MAX.0);
        rv
    }

    const fn rt_from_u32_const(val: u32) -> Self {
        let rv = Self(const_conversions::i32_from_u32(val));
        assert!(rv.0 as u32 == val);
        assert!(rv.0 > Self::STANDARD_MAX.0);
        rv
    }

    /// Returns the raw signal integer or 0.
    pub fn as_raw(this: Option<Self>) -> i32 {
        this.map(|s| s.as_i32()).unwrap_or(0)
    }
}

bitflags::bitflags! {
    #[repr(transparent)]
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct SigActionFlags: u64 {
        const SA_NOCLDSTOP = const_conversions::u64_from_u32(bindings::LINUX_SA_NOCLDSTOP);
        const SA_NOCLDWAIT = const_conversions::u64_from_u32(bindings::LINUX_SA_NOCLDWAIT);
        const SA_NODEFER = const_conversions::u64_from_u32(bindings::LINUX_SA_NODEFER);
        const SA_ONSTACK = const_conversions::u64_from_u32(bindings::LINUX_SA_ONSTACK);
        const SA_RESETHAND = const_conversions::u64_from_u32(bindings::LINUX_SA_RESETHAND);
        const SA_RESTART = const_conversions::u64_from_u32(bindings::LINUX_SA_RESTART);
        const SA_RESTORER = const_conversions::u64_from_u32(bindings::LINUX_SA_RESTORER);
        const SA_SIGINFO = const_conversions::u64_from_u32(bindings::LINUX_SA_SIGINFO);
    }
}
// SAFETY: bitflags guarantees the internal representation is effectively a u64.
unsafe impl VirtualAddressSpaceIndependent for SigActionFlags {}

/// Describes how a signal was sent.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum SigInfoCode {
    Si(SigInfoCodeSi),
    Ill(SigInfoCodeIll),
    Fpe(SigInfoCodeFpe),
    Segv(SigInfoCodeSegv),
    Bus(SigInfoCodeBus),
    Trap(SigInfoCodeTrap),
    Cld(SigInfoCodeCld),
    Poll(SigInfoCodePoll),
    Sys(SigInfoCodeSys),
}

#[derive(Debug, Copy, Clone)]
pub struct SigInfoCodeFromRawError {
    pub code: i32,
    pub signo: i32,
}

impl SigInfoCode {
    /// The interpretation of the `si_code` field `sigaction` can depend on the `si_signo`
    /// field. Tries to determine the correct interpretation.
    fn try_from_raw(raw_code: i32, raw_signo: i32) -> Result<Self, SigInfoCodeFromRawError> {
        // These codes always take precedence, e.g. covering the case where
        // a SIGCHLD is sent via `kill`. The code values are mutually exclusive
        // from the other sets below.
        if let Ok(si_code) = SigInfoCodeSi::try_from(raw_code) {
            return Ok(SigInfoCode::Si(si_code));
        }

        let err = SigInfoCodeFromRawError {
            code: raw_code,
            signo: raw_signo,
        };

        let Ok(signal) = Signal::try_from(raw_signo) else {
            return Err(err);
        };

        // Remaining sets of codes are *not* mutually exclusive, and depend on the signal.
        match signal {
            Signal::SIGCHLD => SigInfoCodeCld::try_from(raw_code)
                .map(SigInfoCode::Cld)
                .or(Err(err)),
            Signal::SIGILL => SigInfoCodeIll::try_from(raw_code)
                .map(SigInfoCode::Ill)
                .or(Err(err)),
            Signal::SIGFPE => SigInfoCodeFpe::try_from(raw_code)
                .map(SigInfoCode::Fpe)
                .or(Err(err)),
            Signal::SIGSEGV => SigInfoCodeSegv::try_from(raw_code)
                .map(SigInfoCode::Segv)
                .or(Err(err)),
            Signal::SIGBUS => SigInfoCodeBus::try_from(raw_code)
                .map(SigInfoCode::Bus)
                .or(Err(err)),
            Signal::SIGTRAP => SigInfoCodeTrap::try_from(raw_code)
                .map(SigInfoCode::Trap)
                .or(Err(err)),
            Signal::SIGPOLL => SigInfoCodePoll::try_from(raw_code)
                .map(SigInfoCode::Poll)
                .or(Err(err)),
            Signal::SIGSYS => SigInfoCodeSys::try_from(raw_code)
                .map(SigInfoCode::Sys)
                .or(Err(err)),
            _ => Err(err),
        }
    }
}

#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeSi {
    // sigaction(2): kill(2)
    SI_USER = const_conversions::i32_from_u32(bindings::LINUX_SI_USER),
    // sigaction(2): Sent by the kernel.
    SI_KERNEL = const_conversions::i32_from_u32(bindings::LINUX_SI_KERNEL),
    // sigaction(2): sigqueue(3)
    SI_QUEUE = bindings::LINUX_SI_QUEUE,
    // sigaction(2): POSIX timer expired.
    SI_TIMER = bindings::LINUX_SI_TIMER,
    // sigaction(2): POSIX message queue state changed; see mq_notify(3)
    SI_MESGQ = bindings::LINUX_SI_MESGQ,
    // sigaction(2): AIO completed
    SI_ASYNCIO = bindings::LINUX_SI_ASYNCIO,
    // sigaction(2): tkill(2) or tgkill(2).
    SI_TKILL = bindings::LINUX_SI_TKILL,
}

/// Codes for SIGCHLD
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeCld {
    // Child has exited.
    CLD_EXITED = const_conversions::i32_from_u32(bindings::LINUX_CLD_EXITED),
    // Child was killed.
    CLD_KILLED = const_conversions::i32_from_u32(bindings::LINUX_CLD_KILLED),
    // Child terminated abnormally.
    CLD_DUMPED = const_conversions::i32_from_u32(bindings::LINUX_CLD_DUMPED),
    // Traced child has trapped.
    CLD_TRAPPED = const_conversions::i32_from_u32(bindings::LINUX_CLD_TRAPPED),
    // Child has stopped.
    CLD_STOPPED = const_conversions::i32_from_u32(bindings::LINUX_CLD_STOPPED),
    // Stopped child has continued.
    CLD_CONTINUED = const_conversions::i32_from_u32(bindings::LINUX_CLD_CONTINUED),
}

/// Codes for SIGILL
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeIll {
    ILL_ILLOPC = const_conversions::i32_from_u32(bindings::LINUX_ILL_ILLOPC),
    ILL_ILLOPN = const_conversions::i32_from_u32(bindings::LINUX_ILL_ILLOPN),
    ILL_ILLADR = const_conversions::i32_from_u32(bindings::LINUX_ILL_ILLADR),
    ILL_ILLTRP = const_conversions::i32_from_u32(bindings::LINUX_ILL_ILLTRP),
    ILL_PRVOPC = const_conversions::i32_from_u32(bindings::LINUX_ILL_PRVOPC),
    ILL_PRVREG = const_conversions::i32_from_u32(bindings::LINUX_ILL_PRVREG),
    ILL_COPROC = const_conversions::i32_from_u32(bindings::LINUX_ILL_COPROC),
    ILL_BADSTK = const_conversions::i32_from_u32(bindings::LINUX_ILL_BADSTK),
    ILL_BADIADDR = const_conversions::i32_from_u32(bindings::LINUX_ILL_BADIADDR),
}

/// Codes for SIGFPE
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeFpe {
    FPE_INTDIV = const_conversions::i32_from_u32(bindings::LINUX_FPE_INTDIV),
    FPE_INTOVF = const_conversions::i32_from_u32(bindings::LINUX_FPE_INTOVF),
    FPE_FLTDIV = const_conversions::i32_from_u32(bindings::LINUX_FPE_FLTDIV),
    FPE_FLTOVF = const_conversions::i32_from_u32(bindings::LINUX_FPE_FLTOVF),
    FPE_FLTUND = const_conversions::i32_from_u32(bindings::LINUX_FPE_FLTUND),
    FPE_FLTRES = const_conversions::i32_from_u32(bindings::LINUX_FPE_FLTRES),
    FPE_FLTINV = const_conversions::i32_from_u32(bindings::LINUX_FPE_FLTINV),
    FPE_FLTSUB = const_conversions::i32_from_u32(bindings::LINUX_FPE_FLTSUB),
    FPE_FLTUNK = const_conversions::i32_from_u32(bindings::LINUX_FPE_FLTUNK),
    FPE_CONDTRAP = const_conversions::i32_from_u32(bindings::LINUX_FPE_CONDTRAP),
}

/// Codes for SIGSEGV
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeSegv {
    SEGV_MAPERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_MAPERR),
    SEGV_ACCERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_ACCERR),
    SEGV_BNDERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_BNDERR),
    SEGV_PKUERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_PKUERR),
    SEGV_ACCADI = const_conversions::i32_from_u32(bindings::LINUX_SEGV_ACCADI),
    SEGV_ADIDERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_ADIDERR),
    SEGV_ADIPERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_ADIPERR),
    SEGV_MTEAERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_MTEAERR),
    SEGV_MTESERR = const_conversions::i32_from_u32(bindings::LINUX_SEGV_MTESERR),
}

/// Codes for SIGBUS
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeBus {
    BUS_ADRALN = const_conversions::i32_from_u32(bindings::LINUX_BUS_ADRALN),
    BUS_ADRERR = const_conversions::i32_from_u32(bindings::LINUX_BUS_ADRERR),
    BUS_OBJERR = const_conversions::i32_from_u32(bindings::LINUX_BUS_OBJERR),
    BUS_MCEERR_AR = const_conversions::i32_from_u32(bindings::LINUX_BUS_MCEERR_AR),
    BUS_MCEERR_AO = const_conversions::i32_from_u32(bindings::LINUX_BUS_MCEERR_AO),
}

/// Codes for SIGTRAP
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeTrap {
    TRAP_BRKPT = const_conversions::i32_from_u32(bindings::LINUX_TRAP_BRKPT),
    TRAP_TRACE = const_conversions::i32_from_u32(bindings::LINUX_TRAP_TRACE),
    TRAP_BRANCH = const_conversions::i32_from_u32(bindings::LINUX_TRAP_BRANCH),
    TRAP_HWBKPT = const_conversions::i32_from_u32(bindings::LINUX_TRAP_HWBKPT),
    TRAP_UNK = const_conversions::i32_from_u32(bindings::LINUX_TRAP_UNK),
    TRAP_PERF = const_conversions::i32_from_u32(bindings::LINUX_TRAP_PERF),
}

/// Codes for SIGIO/SIGPOLL
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodePoll {
    POLL_IN = const_conversions::i32_from_u32(bindings::LINUX_POLL_IN),
    POLL_OUT = const_conversions::i32_from_u32(bindings::LINUX_POLL_OUT),
    POLL_MSG = const_conversions::i32_from_u32(bindings::LINUX_POLL_MSG),
    POLL_ERR = const_conversions::i32_from_u32(bindings::LINUX_POLL_ERR),
    POLL_PRI = const_conversions::i32_from_u32(bindings::LINUX_POLL_PRI),
    POLL_HUP = const_conversions::i32_from_u32(bindings::LINUX_POLL_HUP),
}

/// Codes for SIGSYS
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeSys {
    SYS_SECCOMP = const_conversions::i32_from_u32(bindings::LINUX_SYS_SECCOMP),
}

#[allow(non_camel_case_types)]
pub type linux_siginfo_t = bindings::linux_siginfo_t;

type SigInfoDetailsFields = bindings::linux___sifields;

// The fields of `linux___sifields` in the original Linux source are anonymous
// unions, which aren't supported in Rust.  bindgen generates names for these
// union types. We create aliases here so that we only have to name them in one
// place, and fix them in one place if the names change due to field insertion
// or reordering in some later version of the Linux source.
//
// e.g. in the current version of the generated and mangled kernel bindings, this
// is the declaration of `linux___sifields`:
//
// pub union linux___sifields {
//     pub l_kill: linux___sifields__bindgen_ty_1,
//     pub l_timer: linux___sifields__bindgen_ty_2,
//     pub l_rt: linux___sifields__bindgen_ty_3,
//     pub l_sigchld: linux___sifields__bindgen_ty_4,
//     pub l_sigfault: linux___sifields__bindgen_ty_5,
//     pub l_sigpoll: linux___sifields__bindgen_ty_6,
//     pub l_sigsys: linux___sifields__bindgen_ty_7,
// }
pub type SigInfoDetailsKill = bindings::linux___sifields__bindgen_ty_1;
pub type SigInfoDetailsTimer = bindings::linux___sifields__bindgen_ty_2;
pub type SigInfoDetailsRt = bindings::linux___sifields__bindgen_ty_3;
pub type SigInfoDetailsSigChld = bindings::linux___sifields__bindgen_ty_4;
pub type SigInfoDetailsSigFault = bindings::linux___sifields__bindgen_ty_5;
pub type SigInfoDetailsSigPoll = bindings::linux___sifields__bindgen_ty_6;
pub type SigInfoDetailsSigSys = bindings::linux___sifields__bindgen_ty_7;

pub enum SigInfoDetails {
    Kill(SigInfoDetailsKill),
    Timer(SigInfoDetailsTimer),
    Rt(SigInfoDetailsRt),
    SigChld(SigInfoDetailsSigChld),
    SigFault(SigInfoDetailsSigFault),
    SigPoll(SigInfoDetailsSigPoll),
    SigSys(SigInfoDetailsSigSys),
}

/// Wrapper around `linux_siginfo_t`.
///
/// # Invariants
///
/// The following invariants in the internal `linux_siginfo_t` are ensured when
/// constructed through safe constructors:
///
/// * `lsi_code`, `lsi_signo`, and `lsi_errno` are initialized.
/// * The parts of the `l_sifields` union are initialized that `sigaction(2)`
///   specifies are initialized for the given `lsi_code` and `lsi_signo`.
///
/// When constructing via one of the unsafe constructors that takes some form of
/// `linux_siginfo_t` or the pieces of one, the above must also hold. However,
/// it is sufficient for all bytes to be initialized; any bit pattern is
/// acceptable as long as it is an initialized one. (Initializing from garbage data
/// may result in garbage pointers, but the safe methods of this type will never
/// dereference those itself). For example, `unsafe {
/// SigInfo::wrap_assume_initd(core::mem::zeroed()) }` is sound.
#[derive(Copy, Clone, Debug)]
#[repr(transparent)]
#[allow(non_camel_case_types)]
pub struct siginfo_t(linux_siginfo_t);
// Contains pointers, but they are understood to not necessarily be valid in the
// current address space.
unsafe impl Send for siginfo_t {}
unsafe impl Pod for siginfo_t {}

impl siginfo_t {
    /// The bindings end up with a couple extra outer layers of unions.
    /// The outermost only has a single member; the next one has a data
    /// field and a padding field.
    fn inner(&self) -> &bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
        // SAFETY: Guaranteed initialized by [`SigInfo`] invariants.
        unsafe { &self.0.l__bindgen_anon_1.l__bindgen_anon_1 }
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// See [`siginfo_t`] `Invariants`.
    pub unsafe fn wrap_assume_initd(si: linux_siginfo_t) -> Self {
        Self(si)
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_ref`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// See [`siginfo_t`] `Invariants`.
    pub unsafe fn wrap_ref_assume_initd(si: &linux_siginfo_t) -> &Self {
        unsafe { &*(core::ptr::from_ref(si) as *const Self) }
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_mut`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// See [`siginfo_t`] `Invariants`.
    pub unsafe fn wrap_mut_assume_initd(si: &mut linux_siginfo_t) -> &mut Self {
        unsafe { &mut *(core::ptr::from_mut(si) as *mut Self) }
    }

    /// # Safety
    ///
    /// Pointers are safe to dereference iff those used to construct `si` are.
    pub unsafe fn peel(si: Self) -> linux_siginfo_t {
        si.0
    }

    #[inline]
    pub fn signal(&self) -> Result<Signal, SignalFromI32Error> {
        Signal::try_from(self.inner().lsi_signo)
    }

    #[inline]
    pub fn code(&self) -> Result<SigInfoCode, SigInfoCodeFromRawError> {
        SigInfoCode::try_from_raw(self.inner().lsi_code, self.inner().lsi_signo)
    }

    /// # Safety
    ///
    /// Pointers are safe to dereference iff those used to construct `self` (or set
    /// via mutable methods) are.
    pub unsafe fn details(&self) -> Option<SigInfoDetails> {
        let Ok(code) = self.code() else {
            return None;
        };
        match code {
            SigInfoCode::Si(SigInfoCodeSi::SI_USER) => Some(SigInfoDetails::Kill(unsafe {
                self.inner().l_sifields.l_kill
            })),
            SigInfoCode::Si(SigInfoCodeSi::SI_KERNEL) => Some(SigInfoDetails::SigFault(unsafe {
                self.inner().l_sifields.l_sigfault
            })),
            SigInfoCode::Si(SigInfoCodeSi::SI_QUEUE) => {
                Some(SigInfoDetails::Rt(unsafe { self.inner().l_sifields.l_rt }))
            }
            SigInfoCode::Si(SigInfoCodeSi::SI_TIMER) => Some(SigInfoDetails::Timer(unsafe {
                self.inner().l_sifields.l_timer
            })),
            SigInfoCode::Si(SigInfoCodeSi::SI_MESGQ) => {
                Some(SigInfoDetails::Rt(unsafe { self.inner().l_sifields.l_rt }))
            }
            SigInfoCode::Si(SigInfoCodeSi::SI_ASYNCIO) => Some(SigInfoDetails::SigPoll(unsafe {
                self.inner().l_sifields.l_sigpoll
            })),
            SigInfoCode::Si(SigInfoCodeSi::SI_TKILL) => Some(SigInfoDetails::Kill(unsafe {
                self.inner().l_sifields.l_kill
            })),
            SigInfoCode::Cld(_) => Some(SigInfoDetails::SigChld(unsafe {
                self.inner().l_sifields.l_sigchld
            })),
            // TODO: `l_sigfault` contains another union. Would be nice
            // to safely pick it apart further here.
            SigInfoCode::Ill(_) => Some(SigInfoDetails::SigFault(unsafe {
                self.inner().l_sifields.l_sigfault
            })),
            SigInfoCode::Fpe(_) => Some(SigInfoDetails::SigFault(unsafe {
                self.inner().l_sifields.l_sigfault
            })),
            SigInfoCode::Segv(_) => Some(SigInfoDetails::SigFault(unsafe {
                self.inner().l_sifields.l_sigfault
            })),
            SigInfoCode::Bus(_) => Some(SigInfoDetails::SigFault(unsafe {
                self.inner().l_sifields.l_sigfault
            })),
            SigInfoCode::Trap(_) => Some(SigInfoDetails::SigFault(unsafe {
                self.inner().l_sifields.l_sigfault
            })),
            SigInfoCode::Poll(_) => Some(SigInfoDetails::SigFault(unsafe {
                self.inner().l_sifields.l_sigfault
            })),
            SigInfoCode::Sys(_) => Some(SigInfoDetails::SigSys(unsafe {
                self.inner().l_sifields.l_sigsys
            })),
        }
    }

    // SAFETY: `fields` must be initialized consistently with [`SigInfo`] `Invariants`.
    unsafe fn new(signal: Signal, errno: i32, code: i32, fields: SigInfoDetailsFields) -> Self {
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: signal.into(),
                    lsi_errno: errno,
                    lsi_code: code,
                    l_sifields: fields,
                },
            },
        })
    }

    // TODO: Should `sender_pid` actually be `sender_tid`?
    pub fn new_for_kill(signal: Signal, sender_pid: i32, sender_uid: u32) -> Self {
        // sigaction(2):
        // > Signals  sent  with  kill(2) and sigqueue(3) fill in si_pid and si_uid.
        // > In addition, signals sent with sigqueue(3) fill in si_int and si_ptr with
        // > the values specified by the sender of the signal; see sigqueue(3) for
        // > more details.
        unsafe {
            Self::new(
                signal,
                0,
                SigInfoCodeSi::SI_USER.into(),
                SigInfoDetailsFields {
                    l_kill: SigInfoDetailsKill {
                        l_pid: sender_pid,
                        l_uid: sender_uid,
                    },
                },
            )
        }
    }

    // TODO: Should `sender_pid` actually be `sender_tid`?
    pub fn new_for_tkill(signal: Signal, sender_pid: i32, sender_uid: u32) -> Self {
        unsafe {
            Self::new(
                signal,
                0,
                SigInfoCodeSi::SI_TKILL.into(),
                SigInfoDetailsFields {
                    l_kill: SigInfoDetailsKill {
                        l_pid: sender_pid,
                        l_uid: sender_uid,
                    },
                },
            )
        }
    }

    pub fn new_for_timer(signal: Signal, timer_id: i32, overrun: i32) -> Self {
        // sigaction(2):
        // > Signals sent by POSIX.1b timers (since Linux 2.6) fill in si_overrun and
        // > si_timerid.  The si_timerid field is  an  internal ID  used by the kernel
        // > to identify the timer; it is not the same as the timer ID returned by
        // > timer_create(2).  The si_overrun field is the timer overrun count; this
        // > is the same information as is obtained by a call to timer_getoverrun(2).
        // > These fields are nonstandard Linux extensions.
        unsafe {
            Self::new(
                signal,
                0,
                SigInfoCodeSi::SI_TIMER.into(),
                SigInfoDetailsFields {
                    l_timer: SigInfoDetailsTimer {
                        l_tid: timer_id,
                        l_overrun: overrun,
                        l_sigval: core::mem::zeroed(),
                        l_sys_private: 0,
                    },
                },
            )
        }
    }

    // TODO: Should `sender_pid` actually be `sender_tid`?
    pub fn new_for_mq(
        signal: Signal,
        sender_pid: i32,
        sender_uid: u32,
        sigval: linux_sigval,
    ) -> Self {
        // sigaction(2):
        // > Signals  sent  for  message queue notification (see the description of
        // > SIGEV_SIGNAL in mq_notify(3)) fill in si_int/si_ptr, with the sigev_value
        // > supplied to mq_notify(3); si_pid, with the process ID of the message
        // > sender; and si_uid, with the real user ID of the message sender.
        unsafe {
            Self::new(
                signal,
                0,
                SigInfoCodeSi::SI_MESGQ.into(),
                SigInfoDetailsFields {
                    l_rt: SigInfoDetailsRt {
                        l_pid: sender_pid,
                        l_uid: sender_uid,
                        l_sigval: sigval,
                    },
                },
            )
        }
    }

    pub fn new_for_sigchld_exited(
        exit_signal: Signal,
        child_pid: i32,
        child_uid: u32,
        child_exit_status: i32,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        // sigaction(2):
        // > SIGCHLD  fills  in  si_pid,  si_uid,  si_status, si_utime, and si_stime,
        // > providing information about the child.  The si_pid field is the process
        // > ID of the child; si_uid is the child's real user ID.  The si_status field
        // > contains the exit status  of the  child  (if  si_code  is  CLD_EXITED),
        // ...
        // > The si_utime and si_stime contain the user and system CPU time used by the
        // > child process; these fields do not  include  the  times  used  by
        // > waited-for  children  (unlike  getrusage(2) and times(2)).
        unsafe {
            Self::new(
                exit_signal,
                0,
                SigInfoCodeCld::CLD_EXITED.into(),
                SigInfoDetailsFields {
                    l_sigchld: SigInfoDetailsSigChld {
                        l_pid: child_pid,
                        l_uid: child_uid,
                        l_status: child_exit_status,
                        l_utime: child_utime,
                        l_stime: child_stime,
                    },
                },
            )
        }
    }

    // sigaction(2):
    // > SIGCHLD  fills  in  si_pid,  si_uid,  si_status, si_utime, and si_stime,
    // > providing information about the child.  The si_pid field is the process
    // > ID of the child; si_uid is the child's real user ID. The si_status field
    // > contains
    // ...
    // > the signal number that caused the process to change state.  The
    // > si_utime and si_stime contain the user and system CPU time used by the
    // > child process; these fields do not  include  the  times  used  by
    // > waited-for  children  (unlike  getrusage(2) and times(2)).
    fn new_for_sigchld_signaled(
        exit_signal: Signal,
        code: SigInfoCodeCld,
        child_pid: i32,
        child_uid: u32,
        signal: Signal,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        unsafe {
            Self::new(
                exit_signal,
                0,
                code.into(),
                SigInfoDetailsFields {
                    l_sigchld: SigInfoDetailsSigChld {
                        l_pid: child_pid,
                        l_uid: child_uid,
                        l_status: signal.into(),
                        l_utime: child_utime,
                        l_stime: child_stime,
                    },
                },
            )
        }
    }

    pub fn new_for_sigchld_killed(
        exit_signal: Signal,
        child_pid: i32,
        child_uid: u32,
        fatal_signal: Signal,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            exit_signal,
            SigInfoCodeCld::CLD_KILLED,
            child_pid,
            child_uid,
            fatal_signal,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_dumped(
        exit_signal: Signal,
        child_pid: i32,
        child_uid: u32,
        fatal_signal: Signal,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            exit_signal,
            SigInfoCodeCld::CLD_DUMPED,
            child_pid,
            child_uid,
            fatal_signal,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_trapped(
        exit_signal: Signal,
        child_pid: i32,
        child_uid: u32,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            exit_signal,
            SigInfoCodeCld::CLD_TRAPPED,
            child_pid,
            child_uid,
            Signal::SIGTRAP,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_stopped(
        exit_signal: Signal,
        child_pid: i32,
        child_uid: u32,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            exit_signal,
            SigInfoCodeCld::CLD_STOPPED,
            child_pid,
            child_uid,
            Signal::SIGSTOP,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_continued(
        exit_signal: Signal,
        child_pid: i32,
        child_uid: u32,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            exit_signal,
            SigInfoCodeCld::CLD_CONTINUED,
            child_pid,
            child_uid,
            Signal::SIGCONT,
            child_utime,
            child_stime,
        )
    }

    // TODO: (see sigaction(2))
    // * new_for_sigill
    // * new_for_sigfpe
    // * new_for_sigsegv
    // * new_for_sigtrap
    // * new_for_poll
    // * new_for_seccomp
    // ...
}

impl Default for siginfo_t {
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
#[allow(non_camel_case_types)]
pub struct sigset_t(linux_sigset_t);
unsafe impl TransparentWrapper<linux_sigset_t> for sigset_t {}
unsafe impl shadow_pod::Pod for sigset_t {}

impl sigset_t {
    pub const EMPTY: Self = Self(0);
    pub const FULL: Self = Self(!0);

    pub fn has(&self, sig: Signal) -> bool {
        (*self & sigset_t::from(sig)).0 != 0
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
        *self == sigset_t::EMPTY
    }

    pub fn del(&mut self, sig: Signal) {
        *self &= !sigset_t::from(sig);
    }

    pub fn add(&mut self, sig: Signal) {
        *self |= sigset_t::from(sig);
    }
}

impl From<Signal> for sigset_t {
    #[inline]
    fn from(value: Signal) -> Self {
        let value = i32::from(value);
        debug_assert!(value <= 64);
        Self(1 << (value - 1))
    }
}

#[test]
fn test_from_signal() {
    let sigset = sigset_t::from(Signal::SIGABRT);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(!sigset.has(Signal::SIGSEGV));
    assert_ne!(sigset, sigset_t::EMPTY);
}

impl core::ops::BitOr for sigset_t {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

#[test]
fn test_bitor() {
    let sigset = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
}

impl core::ops::BitOrAssign for sigset_t {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0
    }
}

#[test]
fn test_bitorassign() {
    let mut sigset = sigset_t::from(Signal::SIGABRT);
    sigset |= sigset_t::from(Signal::SIGSEGV);
    assert!(sigset.has(Signal::SIGABRT));
    assert!(sigset.has(Signal::SIGSEGV));
    assert!(!sigset.has(Signal::SIGALRM));
}

impl core::ops::BitAnd for sigset_t {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
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

impl core::ops::BitAndAssign for sigset_t {
    fn bitand_assign(&mut self, rhs: Self) {
        self.0 &= rhs.0
    }
}

#[test]
fn test_bitand_assign() {
    let mut set = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGSEGV);
    set &= sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGALRM);
    assert!(set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(!set.has(Signal::SIGALRM));
}

impl core::ops::Not for sigset_t {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}

#[test]
fn test_not() {
    let set = sigset_t::from(Signal::SIGABRT) | sigset_t::from(Signal::SIGSEGV);
    let set = !set;
    assert!(!set.has(Signal::SIGABRT));
    assert!(!set.has(Signal::SIGSEGV));
    assert!(set.has(Signal::SIGALRM));
}

pub type SignalHandlerFn = unsafe extern "C" fn(i32);
pub type SignalActionFn = unsafe extern "C" fn(i32, *mut siginfo_t, *mut core::ffi::c_void);

pub enum SignalHandler {
    Handler(SignalHandlerFn),
    Action(SignalActionFn),
    SigIgn,
    SigDfl,
}

/// Expose for cbindgen APIs
#[allow(non_camel_case_types)]
pub type linux_sigaction = bindings::linux_sigaction;

/// Restorer suitable for use with `sigaction`.
///
/// Normally libc's implementation of the `sigaction` function injects a similar
/// restorer function.
///
/// From `sigreturn(2)`:
///
/// > If the Linux kernel determines that an unblocked signal is pending for a
/// > process, then, at the next transition back to user mode in that process
/// > (e.g., upon return from a system call or when the process is rescheduled
/// > onto the CPU), it creates a new frame on the user-space stack where it saves
/// > various pieces of process context (processor status word, registers, signal
/// > mask,  and  signal stack settings).
/// >
/// > The  kernel  also  arranges that, during the transition back to user mode,
/// > the signal handler is called, and that, upon return from the handler,
/// > control passes to a piece of user-space code com‐ monly called the "signal
/// > trampoline".  The signal trampoline code in turn calls sigreturn().
/// >
/// > This sigreturn() call undoes everything that was done—changing the
/// > process's signal mask, switching signal stacks (see sigaltstack(2))—in order
/// > to invoke the signal handler.  Using the  informa‐ tion  that was earlier
/// > saved on the user-space stack sigreturn() restores the process's signal
/// > mask, switches stacks, and restores the process's context (processor flags
/// > and registers, including the stack pointer and instruction pointer), so that
/// > the process resumes execution at the point where it was interrupted by the
/// > signal.
///
/// # Safety
///
/// This function is only intended for use as a `restorer` in `sigaction`.
/// Do not call this function directly.
//
// This has to be a `naked` function; the `rt_return` syscall assumes that the
// signal stack frame is at an exact offset from the current stack address; a
// non-naked function would manipulate the stack and break this assumption.
//
// TODO: use the language-provided `naked` attribute if and when one is provided.
// There's been a fair bit of discussion and issues about it, but the current state
// is unclear. See e.g.
// <https://github.com/rust-lang/rfcs/blob/master/text/1201-naked-fns.md>
#[cfg(target_arch = "x86_64")]
#[naked_function::naked]
pub unsafe extern "C" fn sigaction_restorer() {
    // 15 is rt_sigreturn; see static assertion below.
    // The `naked` macro doesn't support putting the assertion here in the
    // function body.
    //
    // The `rt_sigreturn` shouldn't return, but we use `ud2` (illegal
    // instruction) to ensure we don't unexpectedly return in case it does.
    // Strictly speaking the signature of this function could be `-> !`, but
    // that doesn't match the signature expected for the restorer.
    //
    // TODO: use a `const` operand to the asm template instead of inlining "15",
    // once `const` asm template operands are stabilized.
    asm!("mov rax, 15", "syscall", "ud2")
}
static_assertions::const_assert_eq!(bindings::LINUX___NR_rt_sigreturn, 15);

/// # Invariants
///
/// `sigaction` does *not* require or guarantee that its internal function
/// pointers, if any, are safe to call/dereference.
#[derive(Copy, Clone)]
#[repr(C)]
#[allow(non_camel_case_types)]
pub struct sigaction(linux_sigaction);
unsafe impl shadow_pod::Pod for sigaction {}

impl sigaction {
    // Bindgen doesn't succesfully bind these constants; maybe because
    // the macros defining them cast them to pointers.
    //
    // Copied from linux's include/uapi/asm-generic/signal-defs.h.
    const SIG_DFL: usize = 0;
    const SIG_IGN: usize = 1;

    /// Consider [`sigaction::new_with_default_restorer`] instead, which takes
    /// care of setting up a restorer.
    ///
    /// panics if `handler` is inconsistent with the presence or absence of the
    /// `SA_SIGINFO` flag.
    pub fn new_raw(
        handler: SignalHandler,
        flags: SigActionFlags,
        mask: sigset_t,
        restorer: Option<unsafe extern "C" fn()>,
    ) -> Self {
        // The sigaction struct only has a field to hold a handler of type
        // `SignalHandlerFn`, but it can alternatively store a function of type
        // `SignalActionFn` or the integer `SIG_IGN`.
        //
        // We don't have much choice other than to `transmute` here.  We
        // validate the `SA_SIGINFO` flag to ensure we don't reinterpret as the
        // wrong type when extracting from the internal C structure again.
        let handler = match handler {
            SignalHandler::Handler(h) => {
                assert!(!flags.contains(SigActionFlags::SA_SIGINFO));
                Some(h)
            }
            SignalHandler::Action(a) => {
                assert!(flags.contains(SigActionFlags::SA_SIGINFO));
                Some(unsafe { core::mem::transmute::<SignalActionFn, SignalHandlerFn>(a) })
            }
            SignalHandler::SigIgn => {
                assert!(!flags.contains(SigActionFlags::SA_SIGINFO));
                Some(unsafe { core::mem::transmute::<usize, SignalHandlerFn>(Self::SIG_IGN) })
            }
            SignalHandler::SigDfl => {
                assert!(!flags.contains(SigActionFlags::SA_SIGINFO));
                static_assertions::const_assert_eq!(sigaction::SIG_DFL, 0);
                None
            }
        };
        sigaction(linux_sigaction {
            lsa_handler: handler,
            lsa_flags: flags.bits(),
            lsa_mask: mask.0,
            lsa_restorer: restorer,
        })
    }

    /// Creates a `sigaction` with `SA_RESTORER` set, and the internal
    /// `restorer` field set to [`sigaction_restorer`]. The libc `sigaction`
    /// function normally makes these changes to the provided `struct
    /// sigaction`.
    pub fn new_with_default_restorer(
        handler: SignalHandler,
        flags: SigActionFlags,
        mask: sigset_t,
    ) -> Self {
        Self::new_raw(
            handler,
            flags | SigActionFlags::SA_RESTORER,
            mask,
            Some(sigaction_restorer),
        )
    }

    pub fn wrap(si: linux_sigaction) -> Self {
        Self(si)
    }

    pub fn wrap_ref(si: &linux_sigaction) -> &Self {
        unsafe { &*(core::ptr::from_ref(si) as *const Self) }
    }

    pub fn wrap_mut(si: &mut linux_sigaction) -> &mut Self {
        unsafe { &mut *(core::ptr::from_mut(si) as *mut Self) }
    }

    /// # Safety
    ///
    /// `lsa_handler` is safe to dereference iff the `lsa_handler` used to
    /// construct `Self` is.
    pub unsafe fn peel(si: Self) -> linux_sigaction {
        si.0
    }

    pub fn flags(&self) -> Option<SigActionFlags> {
        SigActionFlags::from_bits(self.0.lsa_flags)
    }

    pub fn flags_retain(&self) -> SigActionFlags {
        SigActionFlags::from_bits_retain(self.0.lsa_flags)
    }

    pub fn mask(&self) -> sigset_t {
        sigset_t::wrap(self.0.lsa_mask)
    }

    /// # Safety
    ///
    /// The functions in `SignalHandler::Action` or `SignalHandler::Handler` are
    /// safe to call iff the function pointer in the internal `lsa_handler` is,
    /// and is of the type specified in the internal `lsa_flags`.
    pub unsafe fn handler(&self) -> SignalHandler {
        let as_usize = self.as_usize();
        if as_usize == Self::SIG_IGN {
            SignalHandler::SigIgn
        } else if as_usize == Self::SIG_DFL {
            SignalHandler::SigDfl
        } else if self.flags_retain().contains(SigActionFlags::SA_SIGINFO) {
            // SIG_IGN is, not coincidentally, 0. If we get here, we know it's not 0/NULL.
            // Therefore the `Option` will be non-empty.
            let handler_fn: SignalHandlerFn = self.0.lsa_handler.unwrap();
            // The C bindings only store a single function pointer type, and cast it
            // when appropriate. In Rust this requires a `transmute`.
            //
            // We *don't* know whether the function is actually safe to call. The pointer
            // could be invalid in the current virtual address space, or invalid everywhere.
            // We *do* know it's not NULL.
            let action_fn: SignalActionFn =
                unsafe { core::mem::transmute::<SignalHandlerFn, SignalActionFn>(handler_fn) };
            SignalHandler::Action(action_fn)
        } else {
            SignalHandler::Handler(self.0.lsa_handler.unwrap())
        }
    }

    fn as_usize(&self) -> usize {
        self.0.lsa_handler.map(|f| f as usize).unwrap_or(0)
    }

    pub fn is_ignore(&self) -> bool {
        self.as_usize() == Self::SIG_IGN
    }

    pub fn is_default(&self) -> bool {
        self.as_usize() == Self::SIG_DFL
    }
}

impl Default for sigaction {
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

// Corresponds to default actions documented in signal(7).
#[derive(Eq, PartialEq, Debug)]
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

/// Execute the `kill` syscall.
pub fn kill_raw(pid: kernel_pid_t, sig: i32) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_kill, pid, sig) }
        .check()
        .map_err(Errno::from)
}

/// Execute the `kill` syscall, targeting a process.
pub fn kill_process(pid: Pid, sig: Option<Signal>) -> Result<(), Errno> {
    kill_raw(pid.as_raw_nonzero().into(), Signal::as_raw(sig))
}

/// Execute the `kill` syscall, targeting a process group.
pub fn kill_process_group(pid: Pid, sig: Option<Signal>) -> Result<(), Errno> {
    kill_raw(-i32::from(pid.as_raw_nonzero()), Signal::as_raw(sig))
}

/// Execute the `kill` syscall, targeting the current process group.
pub fn kill_current_process_group(sig: Option<Signal>) -> Result<(), Errno> {
    kill_raw(0, Signal::as_raw(sig))
}

/// Calls the `rt_sigaction` syscall.
///
/// # Safety
///
/// * `new_action` must be safe to dereference.
/// * `old_action` must be safe to write to. (uninitd is ok).
/// * `new_action`'s handler must be safe to call as a signal handler.
///   See `signal-safety(7)`.
/// * Generally, `new_action` must have `SA_RESTORER` set and a suitable
///   `restorer`, such as [`sigaction_restorer`]. (There might be some esoteric
///   way to call this syscall without this property, but I'm not aware of one).
pub unsafe fn rt_sigaction_raw(
    signo: i32,
    new_action: *const sigaction,
    old_action: *mut sigaction,
    sigsetsize: usize,
) -> Result<(), Errno> {
    unsafe {
        syscall!(
            linux_syscall::SYS_rt_sigaction,
            signo,
            new_action,
            old_action,
            sigsetsize
        )
    }
    .check()
    .map_err(Errno::from)
}

/// Calls the `rt_sigaction` syscall.
///
/// # Safety
///
/// * `new_action`'s handler must be safe to call as a signal handler.
///   See `signal-safety(7)`.
/// * Generally, `new_action` must have `SA_RESTORER` set and a suitable
///   `restorer`, such as [`sigaction_restorer`]. (There might be some esoteric
///   way to call this syscall without this property, but I'm not aware of one).
pub unsafe fn rt_sigaction(
    signal: Signal,
    new_action: &sigaction,
    old_action: Option<&mut sigaction>,
) -> Result<(), Errno> {
    unsafe {
        rt_sigaction_raw(
            signal.as_i32(),
            new_action,
            old_action
                .map(core::ptr::from_mut)
                .unwrap_or(core::ptr::null_mut()),
            core::mem::size_of::<sigset_t>(),
        )
    }
}

/// For use with [`rt_sigprocmask`].
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigProcMaskAction {
    SIG_BLOCK = const_conversions::i32_from_u32(bindings::LINUX_SIG_BLOCK),
    SIG_UNBLOCK = const_conversions::i32_from_u32(bindings::LINUX_SIG_UNBLOCK),
    SIG_SETMASK = const_conversions::i32_from_u32(bindings::LINUX_SIG_SETMASK),
}

/// Make the `rt_sigprocmask` syscall.
///
/// # Safety
///
/// * `sigset_in` must be safe to dereference
/// * `sigset_out` must be safe to write (uninit is ok)
pub unsafe fn rt_sigprocmask_raw(
    how: i32,
    sigset_in: *const sigset_t,
    sigset_out: *mut sigset_t,
    sigset_sz: usize,
) -> Result<(), Errno> {
    unsafe {
        syscall!(
            linux_syscall::SYS_rt_sigprocmask,
            how,
            sigset_in,
            sigset_out,
            sigset_sz,
        )
        .check()
        .map_err(Errno::from)
    }
}

/// Make the `rt_sigprocmask` syscall.
pub fn rt_sigprocmask(
    how: SigProcMaskAction,
    sigset_in: &sigset_t,
    sigset_out: Option<&mut sigset_t>,
) -> Result<(), Errno> {
    unsafe {
        rt_sigprocmask_raw(
            how.into(),
            sigset_in,
            sigset_out
                .map(core::ptr::from_mut)
                .unwrap_or(core::ptr::null_mut()),
            core::mem::size_of::<sigset_t>(),
        )
    }
}

#[cfg(test)]
mod rt_sigaction_tests {
    use core::sync::atomic::AtomicI32;

    use shadow_pod::zeroed;

    use super::*;

    // This test calls `rt_sigaction` with `SIGUSR2`. `rt_sigaction` sets the
    // handler *process*-wide, so could interfere if other unit tests use
    // `SIGUSR2`. This is *probably* the only module in this crate that uses
    // signal handling, but we should be careful about creating other tests that
    // do signal handling. e.g. add those to this test to ensure they are
    // effectively serialized, or ensure they use different signals.
    #[cfg(not(miri))]
    #[test]
    fn test_rt_sigaction() {
        // Test signal handler
        static CALL_COUNTER: AtomicI32 = AtomicI32::new(0);
        extern "C" fn handler(_signo: i32) {
            CALL_COUNTER.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
        }

        // Test sigaction
        let action = sigaction::new_with_default_restorer(
            SignalHandler::Handler(handler),
            SigActionFlags::empty(),
            sigset_t::EMPTY,
        );

        // Signal that we'll be using.
        let signal = Signal::SIGUSR2;

        // Install our handler.
        let mut old_action = zeroed();
        unsafe { rt_sigaction(signal, &action, Some(&mut old_action)) }.unwrap();

        // Ensure the signal isn't blocked.
        let mut old_mask: sigset_t = sigset_t::EMPTY;
        let mask = sigset_t::from(signal);
        rt_sigprocmask(SigProcMaskAction::SIG_UNBLOCK, &mask, Some(&mut old_mask)).unwrap();

        // Send the signal to this thread. This should guarantee that the signal
        // is handled before returning from the `tgkill` syscall.
        let pid = rustix::process::getpid();
        let tid = rustix::thread::gettid();
        unsafe {
            linux_syscall::syscall!(
                linux_syscall::SYS_tgkill,
                pid.as_raw_nonzero().get(),
                tid.as_raw_nonzero().get(),
                signal.as_i32()
            )
        }
        .check()
        .unwrap();

        // Validate that our signal handler was called.
        assert_eq!(CALL_COUNTER.load(core::sync::atomic::Ordering::Relaxed), 1);

        // Restore previous signal action and mask.
        rt_sigprocmask(SigProcMaskAction::SIG_SETMASK, &old_mask, None).unwrap();
        unsafe { rt_sigaction(signal, &old_action, None) }.unwrap();
    }
}

pub use bindings::linux_stack_t;
#[allow(non_camel_case_types)]
pub type stack_t = linux_stack_t;

impl stack_t {
    pub fn new(sp: *mut core::ffi::c_void, flags: SigAltStackFlags, size: usize) -> Self {
        Self {
            ss_sp: sp,
            ss_flags: flags.bits(),
            ss_size: size.try_into().unwrap(),
        }
    }

    pub fn flags_retain(&self) -> SigAltStackFlags {
        SigAltStackFlags::from_bits_retain(self.ss_flags)
    }

    pub fn sp(&self) -> *mut core::ffi::c_void {
        self.ss_sp
    }

    pub fn size(&self) -> usize {
        self.ss_size.try_into().unwrap()
    }
}

// bindgen fails to bind this one.
// Copied from linux's include/uapi/linux/signal.h.
pub const LINUX_SS_AUTODISARM: u32 = 1 << 31;
bitflags::bitflags! {
    // While the Linux header defines the only current value of this flag as
    // u32, the field in `stack_t::ss_flags` where this type is used is i32.
    #[repr(transparent)]
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct SigAltStackFlags: i32 {
        // The raw u32 value wraps around to a negative i32.
        const SS_AUTODISARM = i32_from_u32_allowing_wraparound(LINUX_SS_AUTODISARM);
        const SS_ONSTACK = const_conversions::i32_from_u32(bindings::LINUX_SS_ONSTACK);
        const SS_DISABLE= const_conversions::i32_from_u32(bindings::LINUX_SS_DISABLE);
    }
}
// SAFETY: bitflags guarantees the internal representation is effectively a i32.
unsafe impl VirtualAddressSpaceIndependent for SigAltStackFlags {}

/// # Safety
///
/// * `new_stack` must be dereferenceable or NULL.
/// * `old_stack` must be dereferenceable or NULL.
/// * See sigaltstack(2).
pub unsafe fn sigaltstack_raw(
    new_stack: *const stack_t,
    old_stack: *mut stack_t,
) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_sigaltstack, new_stack, old_stack) }
        .check()
        .map_err(Errno::from)
}

/// # Safety
///
/// See sigaltstack(2)
pub unsafe fn sigaltstack(
    new_stack: Option<&stack_t>,
    old_stack: Option<&mut stack_t>,
) -> Result<(), Errno> {
    unsafe {
        sigaltstack_raw(
            new_stack
                .map(core::ptr::from_ref)
                .unwrap_or(core::ptr::null()),
            old_stack
                .map(core::ptr::from_mut)
                .unwrap_or(core::ptr::null_mut()),
        )
    }
}

pub fn tgkill_raw(tgid: kernel_pid_t, tid: kernel_pid_t, signo: i32) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_tgkill, tgid, tid, signo) }
        .check()
        .map_err(Errno::from)
}

pub fn tgkill(tgid: Pid, tid: Pid, signal: Option<Signal>) -> Result<(), Errno> {
    tgkill_raw(
        Pid::as_raw(Some(tgid)),
        Pid::as_raw(Some(tid)),
        signal.map(i32::from).unwrap_or(0),
    )
}

mod export {
    use crate::bindings::{linux_siginfo_t, linux_sigset_t};

    use super::*;

    #[no_mangle]
    pub extern "C-unwind" fn linux_signal_is_valid(signo: i32) -> bool {
        Signal::try_from(signo).is_ok()
    }

    #[no_mangle]
    pub extern "C-unwind" fn linux_signal_is_realtime(signo: i32) -> bool {
        let Ok(signal) = Signal::try_from(signo) else {
            return false;
        };
        signal.is_realtime()
    }

    #[no_mangle]
    pub extern "C-unwind" fn linux_sigemptyset() -> linux_sigset_t {
        sigset_t::EMPTY.0
    }

    #[no_mangle]
    pub extern "C-unwind" fn linux_sigfullset() -> linux_sigset_t {
        sigset_t::FULL.0
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigaddset(set: *mut linux_sigset_t, signo: i32) {
        let set = sigset_t::wrap_mut(unsafe { set.as_mut().unwrap() });
        let signo = Signal::try_from(signo).unwrap();
        set.add(signo);
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigdelset(set: *mut linux_sigset_t, signo: i32) {
        let set = sigset_t::wrap_mut(unsafe { set.as_mut().unwrap() });
        let signo = Signal::try_from(signo).unwrap();
        set.del(signo);
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigismember(
        set: *const linux_sigset_t,
        signo: i32,
    ) -> bool {
        let set = sigset_t::wrap_ref(unsafe { set.as_ref().unwrap() });
        set.has(signo.try_into().unwrap())
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigisemptyset(set: *const linux_sigset_t) -> bool {
        let set = sigset_t::wrap_ref(unsafe { set.as_ref().unwrap() });
        set.is_empty()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigorset(
        lhs: *const linux_sigset_t,
        rhs: *const linux_sigset_t,
    ) -> linux_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        sigset_t(*lhs | *rhs).0
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigandset(
        lhs: *const linux_sigset_t,
        rhs: *const linux_sigset_t,
    ) -> linux_sigset_t {
        let lhs = unsafe { lhs.as_ref().unwrap() };
        let rhs = unsafe { rhs.as_ref().unwrap() };
        sigset_t(*lhs & *rhs).0
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_signotset(set: *const linux_sigset_t) -> linux_sigset_t {
        let set = unsafe { set.as_ref().unwrap() };
        sigset_t(!*set).0
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_siglowest(set: *const linux_sigset_t) -> i32 {
        let set = sigset_t::wrap_ref(unsafe { set.as_ref().unwrap() });
        match set.lowest() {
            Some(s) => s.into(),
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C-unwind" fn linux_defaultAction(signo: i32) -> LinuxDefaultAction {
        let sig = Signal::try_from(signo).unwrap();
        defaultaction(sig)
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_kill(pid: i32, sig: i32) -> i32 {
        match kill_raw(pid, sig) {
            Ok(()) => 0,
            Err(e) => e.to_negated_i32(),
        }
    }

    /// Returns the handler if there is one, or else NULL.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigaction_handler(
        sa: *const linux_sigaction,
    ) -> Option<unsafe extern "C" fn(i32)> {
        let sa = sigaction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        match unsafe { sa.handler() } {
            SignalHandler::Handler(h) => Some(h),
            _ => None,
        }
    }

    /// Returns the action if there is one, else NULL.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigaction_action(
        sa: *const linux_sigaction,
    ) -> Option<unsafe extern "C" fn(i32, *mut linux_siginfo_t, *mut core::ffi::c_void)> {
        let sa = sigaction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        match unsafe { sa.handler() } {
            SignalHandler::Action(h) =>
            // We transmute the function pointer from one that takes SigAction
            // to one that takes linux_sigaction_t. These two types are safely
            // transmutable.
            {
                Some(unsafe {
                    core::mem::transmute::<
                        unsafe extern "C" fn(i32, *mut siginfo_t, *mut core::ffi::c_void),
                        unsafe extern "C" fn(i32, *mut linux_siginfo_t, *mut core::ffi::c_void),
                    >(h)
                })
            }
            _ => None,
        }
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigaction_is_ign(sa: *const linux_sigaction) -> bool {
        let sa = sigaction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        matches!(unsafe { sa.handler() }, SignalHandler::SigIgn)
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_sigaction_is_dfl(sa: *const linux_sigaction) -> bool {
        let sa = sigaction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        matches!(unsafe { sa.handler() }, SignalHandler::SigDfl)
    }
}
