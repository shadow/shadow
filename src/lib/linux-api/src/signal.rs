use bytemuck::TransparentWrapper;
use num_enum::{IntoPrimitive, TryFromPrimitive};
use vasi::VirtualAddressSpaceIndependent;

use crate::bindings::{self, linux_sigval};

/// Definition is sometimes missing in the userspace headers.
//
// bindgen fails to bind this one.
// Copied from linux's include/uapi/linux/signal.h.
pub const LINUX_SS_AUTODISARM: i32 = 1 << 31;

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

    // Helper for declaring aliases below. Validates that `from` and `to` have the
    // same integer value, and returns that value.
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
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct SigActionFlags: u64 {
        const SA_NOCLDSTOP = bindings::LINUX_SA_NOCLDSTOP as u64;
        const SA_NOCLDWAIT = bindings::LINUX_SA_NOCLDWAIT as u64;
        const SA_NODEFER = bindings::LINUX_SA_NODEFER as u64;
        const SA_ONSTACK = bindings::LINUX_SA_ONSTACK as u64;
        const SA_RESETHAND = bindings::LINUX_SA_RESETHAND as u64;
        const SA_RESTART = bindings::LINUX_SA_RESTART as u64;
        const SA_RESTORER = bindings::LINUX_SA_RESTORER as u64;
        const SA_SIGINFO = bindings::LINUX_SA_SIGINFO as u64;
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
    SI_USER = bindings::LINUX_SI_USER as i32,
    // sigaction(2): Sent by the kernel.
    SI_KERNEL = bindings::LINUX_SI_KERNEL as i32,
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
    CLD_EXITED = bindings::LINUX_CLD_EXITED as i32,
    // Child was killed.
    CLD_KILLED = bindings::LINUX_CLD_KILLED as i32,
    // Child terminated abnormally.
    CLD_DUMPED = bindings::LINUX_CLD_DUMPED as i32,
    // Traced child has trapped.
    CLD_TRAPPED = bindings::LINUX_CLD_TRAPPED as i32,
    // Child has stopped.
    CLD_STOPPED = bindings::LINUX_CLD_STOPPED as i32,
    // Stopped child has continued.
    CLD_CONTINUED = bindings::LINUX_CLD_CONTINUED as i32,
}

/// Codes for SIGILL
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeIll {
    ILL_ILLOPC = bindings::LINUX_ILL_ILLOPC as i32,
    ILL_ILLOPN = bindings::LINUX_ILL_ILLOPN as i32,
    ILL_ILLADR = bindings::LINUX_ILL_ILLADR as i32,
    ILL_ILLTRP = bindings::LINUX_ILL_ILLTRP as i32,
    ILL_PRVOPC = bindings::LINUX_ILL_PRVOPC as i32,
    ILL_PRVREG = bindings::LINUX_ILL_PRVREG as i32,
    ILL_COPROC = bindings::LINUX_ILL_COPROC as i32,
    ILL_BADSTK = bindings::LINUX_ILL_BADSTK as i32,
    ILL_BADIADDR = bindings::LINUX_ILL_BADIADDR as i32,
}

/// Codes for SIGFPE
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeFpe {
    FPE_INTDIV = bindings::LINUX_FPE_INTDIV as i32,
    FPE_INTOVF = bindings::LINUX_FPE_INTOVF as i32,
    FPE_FLTDIV = bindings::LINUX_FPE_FLTDIV as i32,
    FPE_FLTOVF = bindings::LINUX_FPE_FLTOVF as i32,
    FPE_FLTUND = bindings::LINUX_FPE_FLTUND as i32,
    FPE_FLTRES = bindings::LINUX_FPE_FLTRES as i32,
    FPE_FLTINV = bindings::LINUX_FPE_FLTINV as i32,
    FPE_FLTSUB = bindings::LINUX_FPE_FLTSUB as i32,
    FPE_FLTUNK = bindings::LINUX_FPE_FLTUNK as i32,
    FPE_CONDTRAP = bindings::LINUX_FPE_CONDTRAP as i32,
}

/// Codes for SIGSEGV
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeSegv {
    SEGV_MAPERR = bindings::LINUX_SEGV_MAPERR as i32,
    SEGV_ACCERR = bindings::LINUX_SEGV_ACCERR as i32,
    SEGV_BNDERR = bindings::LINUX_SEGV_BNDERR as i32,
    SEGV_PKUERR = bindings::LINUX_SEGV_PKUERR as i32,
    SEGV_ACCADI = bindings::LINUX_SEGV_ACCADI as i32,
    SEGV_ADIDERR = bindings::LINUX_SEGV_ADIDERR as i32,
    SEGV_ADIPERR = bindings::LINUX_SEGV_ADIPERR as i32,
    SEGV_MTEAERR = bindings::LINUX_SEGV_MTEAERR as i32,
    SEGV_MTESERR = bindings::LINUX_SEGV_MTESERR as i32,
}

/// Codes for SIGBUS
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeBus {
    BUS_ADRALN = bindings::LINUX_BUS_ADRALN as i32,
    BUS_ADRERR = bindings::LINUX_BUS_ADRERR as i32,
    BUS_OBJERR = bindings::LINUX_BUS_OBJERR as i32,
    BUS_MCEERR_AR = bindings::LINUX_BUS_MCEERR_AR as i32,
    BUS_MCEERR_AO = bindings::LINUX_BUS_MCEERR_AO as i32,
}

/// Codes for SIGTRAP
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeTrap {
    TRAP_BRKPT = bindings::LINUX_TRAP_BRKPT as i32,
    TRAP_TRACE = bindings::LINUX_TRAP_TRACE as i32,
    TRAP_BRANCH = bindings::LINUX_TRAP_BRANCH as i32,
    TRAP_HWBKPT = bindings::LINUX_TRAP_HWBKPT as i32,
    TRAP_UNK = bindings::LINUX_TRAP_UNK as i32,
    TRAP_PERF = bindings::LINUX_TRAP_PERF as i32,
}

/// Codes for SIGIO/SIGPOLL
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodePoll {
    POLL_IN = bindings::LINUX_POLL_IN as i32,
    POLL_OUT = bindings::LINUX_POLL_OUT as i32,
    POLL_MSG = bindings::LINUX_POLL_MSG as i32,
    POLL_ERR = bindings::LINUX_POLL_ERR as i32,
    POLL_PRI = bindings::LINUX_POLL_PRI as i32,
    POLL_HUP = bindings::LINUX_POLL_HUP as i32,
}

/// Codes for SIGSYS
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum SigInfoCodeSys {
    SYS_SECCOMP = bindings::LINUX_SYS_SECCOMP as i32,
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
#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct SigInfo(linux_siginfo_t);
// Contains pointers, but they are understood to not necessarily be valid in the
// current address space.
unsafe impl Send for SigInfo {}

unsafe impl bytemuck::Zeroable for SigInfo {}
unsafe impl bytemuck::AnyBitPattern for SigInfo {}
unsafe impl bytemuck_util::AnyBitPattern for SigInfo {}

impl SigInfo {
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
    /// See [`SigInfo`] `Invariants`.
    pub unsafe fn wrap_assume_initd(si: linux_siginfo_t) -> Self {
        Self(si)
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_ref`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// See [`SigInfo`] `Invariants`.
    pub unsafe fn wrap_ref_assume_initd(si: &linux_siginfo_t) -> &Self {
        unsafe { &*(si as *const _ as *const Self) }
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_mut`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// See [`SigInfo`] `Invariants`.
    pub unsafe fn wrap_mut_assume_initd(si: &mut linux_siginfo_t) -> &mut Self {
        unsafe { &mut *(si as *mut _ as *mut Self) }
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
                Signal::SIGCHLD,
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
        code: SigInfoCodeCld,
        child_pid: i32,
        child_uid: u32,
        signal: Signal,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        unsafe {
            Self::new(
                Signal::SIGCHLD,
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
        child_pid: i32,
        child_uid: u32,
        fatal_signal: Signal,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            SigInfoCodeCld::CLD_KILLED,
            child_pid,
            child_uid,
            fatal_signal,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_dumped(
        child_pid: i32,
        child_uid: u32,
        fatal_signal: Signal,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            SigInfoCodeCld::CLD_DUMPED,
            child_pid,
            child_uid,
            fatal_signal,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_trapped(
        child_pid: i32,
        child_uid: u32,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            SigInfoCodeCld::CLD_TRAPPED,
            child_pid,
            child_uid,
            Signal::SIGTRAP,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_stopped(
        child_pid: i32,
        child_uid: u32,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
            SigInfoCodeCld::CLD_STOPPED,
            child_pid,
            child_uid,
            Signal::SIGSTOP,
            child_utime,
            child_stime,
        )
    }
    pub fn new_for_sigchld_continued(
        child_pid: i32,
        child_uid: u32,
        child_utime: i64,
        child_stime: i64,
    ) -> Self {
        Self::new_for_sigchld_signaled(
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
unsafe impl bytemuck::Zeroable for SigSet {}
unsafe impl bytemuck::AnyBitPattern for SigSet {}
unsafe impl bytemuck_util::AnyBitPattern for SigSet {}

impl SigSet {
    pub const EMPTY: Self = Self(0);
    pub const FULL: Self = Self(!0);

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

/// # Invariants
///
/// `SigAction` does *not* require or guarantee that its internal function
/// pointer, if any, is safe to call/dereference.
#[derive(Copy, Clone)]
#[repr(C)]
pub struct SigAction(linux_sigaction);
unsafe impl Send for SigAction {}
unsafe impl bytemuck::Zeroable for SigAction {}
unsafe impl bytemuck::AnyBitPattern for SigAction {}
unsafe impl bytemuck_util::AnyBitPattern for SigAction {}

impl SigAction {
    // Bindgen doesn't succesfully bind these constants; maybe because
    // the macros defining them cast them to pointers.
    //
    // Copied from linux's include/uapi/asm-generic/signal-defs.h.
    const SIG_DFL: usize = 0;
    const SIG_IGN: usize = 1;

    pub fn wrap(si: linux_sigaction) -> Self {
        Self(si)
    }

    pub fn wrap_ref(si: &linux_sigaction) -> &Self {
        unsafe { &*(si as *const _ as *const Self) }
    }

    pub fn wrap_mut(si: &mut linux_sigaction) -> &mut Self {
        unsafe { &mut *(si as *mut _ as *mut Self) }
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

    /// # Safety
    ///
    /// The functions in `SignalHandler::Action` or `SignalHandler::Handler` are
    /// safe to call iff the function pointer in the internal `lsa_handler` is,
    /// and is of the type specified in the internal `lsa_flags`.
    pub unsafe fn handler(&self) -> SignalHandler {
        let as_usize = self.0.lsa_handler.map(|f| f as usize).unwrap_or(0);
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
        let set = SigSet::wrap_mut(unsafe { set.as_mut().unwrap() });
        let signo = Signal::try_from(signo).unwrap();
        set.add(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigdelset(set: *mut linux_sigset_t, signo: i32) {
        let set = SigSet::wrap_mut(unsafe { set.as_mut().unwrap() });
        let signo = Signal::try_from(signo).unwrap();
        set.del(signo);
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigismember(set: *const linux_sigset_t, signo: i32) -> bool {
        let set = SigSet::wrap_ref(unsafe { set.as_ref().unwrap() });
        set.has(signo.try_into().unwrap())
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigisemptyset(set: *const linux_sigset_t) -> bool {
        let set = SigSet::wrap_ref(unsafe { set.as_ref().unwrap() });
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
        let set = SigSet::wrap_ref(unsafe { set.as_ref().unwrap() });
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
    pub extern "C" fn linux_siginfo_new_for_kill(
        lsi_signo: i32,
        sender_pid: i32,
        sender_uid: u32,
    ) -> linux_siginfo_t {
        let signal = Signal::try_from(lsi_signo).unwrap();
        unsafe { SigInfo::peel(SigInfo::new_for_kill(signal, sender_pid, sender_uid)) }
    }

    #[no_mangle]
    pub extern "C" fn linux_siginfo_new_for_tkill(
        lsi_signo: i32,
        sender_pid: i32,
        sender_uid: u32,
    ) -> linux_siginfo_t {
        let signal = Signal::try_from(lsi_signo).unwrap();
        unsafe { SigInfo::peel(SigInfo::new_for_tkill(signal, sender_pid, sender_uid)) }
    }

    /// Returns the handler if there is one, or else NULL.
    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaction_handler(
        sa: *const linux_sigaction,
    ) -> Option<unsafe extern "C" fn(i32)> {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        match unsafe { sa.handler() } {
            SignalHandler::Handler(h) => Some(h),
            _ => None,
        }
    }

    /// Returns the action if there is one, else NULL.
    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaction_action(
        sa: *const linux_sigaction,
    ) -> Option<unsafe extern "C" fn(i32, *mut linux_siginfo_t, *mut core::ffi::c_void)> {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        match unsafe { sa.handler() } {
            SignalHandler::Action(h) =>
            // We transmute the function pointer from one that takes SigAction
            // to one that takes linux_sigaction_t. These two types are safely
            // transmutable.
            {
                Some(unsafe {
                    core::mem::transmute::<
                        unsafe extern "C" fn(i32, *mut SigInfo, *mut core::ffi::c_void),
                        unsafe extern "C" fn(i32, *mut linux_siginfo_t, *mut core::ffi::c_void),
                    >(h)
                })
            }
            _ => None,
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaction_is_ign(sa: *const linux_sigaction) -> bool {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        matches!(unsafe { sa.handler() }, SignalHandler::SigIgn)
    }

    #[no_mangle]
    pub unsafe extern "C" fn linux_sigaction_is_dfl(sa: *const linux_sigaction) -> bool {
        let sa = SigAction::wrap_ref(unsafe { sa.as_ref().unwrap() });
        matches!(unsafe { sa.handler() }, SignalHandler::SigDfl)
    }
}
