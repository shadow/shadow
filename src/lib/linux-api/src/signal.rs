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
    Unknown(i32),
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
    SI_QUEUE = bindings::LINUX_SI_QUEUE as i32,
    // sigaction(2): POSIX timer expired.
    SI_TIMER = bindings::LINUX_SI_TIMER as i32,
    // sigaction(2): POSIX message queue state changed; see mq_notify(3)
    SI_MESGQ = bindings::LINUX_SI_MESGQ as i32,
    // sigaction(2): AIO completed
    SI_ASYNCIO = bindings::LINUX_SI_ASYNCIO as i32,
    // sigaction(2): tkill(2) or tgkill(2).
    SI_TKILL = bindings::LINUX_SI_TKILL as i32,
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

pub type SigInfoDetailsKill = bindings::linux__sifields__bindgen_ty_1;
pub type SigInfoDetailsTimer = bindings::linux__sifields__bindgen_ty_2;
pub type SigInfoDetailsRt = bindings::linux__sifields__bindgen_ty_3;
pub type SigInfoDetailsSigChld = bindings::linux__sifields__bindgen_ty_4;
pub type SigInfoDetailsSigFault = bindings::linux__sifields__bindgen_ty_5;
pub type SigInfoDetailsSigPoll = bindings::linux__sifields__bindgen_ty_6;
pub type SigInfoDetailsSigSys = bindings::linux__sifields__bindgen_ty_7;

pub enum SigInfoDetails {
    Kill(SigInfoDetailsKill),
    Timer(SigInfoDetailsTimer),
    Rt(SigInfoDetailsRt),
    SigChld(SigInfoDetailsSigChld),
    SigFault(SigInfoDetailsSigFault),
    SigPoll(SigInfoDetailsSigPoll),
    SigSys(SigInfoDetailsSigSys),
}

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

    /// Analogous to `bytemuck::TransparentWrapper::wrap`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// At least the bytes specified by sigaction(2) are initialized.
    pub unsafe fn wrap_assume_initd(si: linux_siginfo_t) -> Self {
        Self(si)
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_ref`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// At least the bytes specified by sigaction(2) are initialized.
    pub unsafe fn wrap_ref_assume_initd(si: &linux_siginfo_t) -> &Self {
        unsafe { &*(si as *const _ as *const Self) }
    }

    /// Analogous to `bytemuck::TransparentWrapper::wrap_mut`, but `unsafe`.
    ///
    /// # Safety
    ///
    /// At least the bytes specified by sigaction(2) are initialized.
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
    pub fn signal(&self) -> Signal {
        Signal(self.inner().lsi_signo)
    }

    #[inline]
    pub fn code(&self) -> SigInfoCode {
        let raw_code = self.inner().lsi_code;

        // These codes always take precedence, e.g. covering the case where
        // a SIGCHLD is sent via `kill`. The code values are mutually exclusive
        // from the other sets below.
        if let Ok(si_code) = SigInfoCodeSi::try_from(raw_code) {
            return SigInfoCode::Si(si_code);
        }

        // Remaining sets of codes are *not* mutually exclusive, and depend on the signal.
        match self.signal() {
            Signal::SIGCHLD => {
                if let Ok(cld_code) = SigInfoCodeCld::try_from(raw_code) {
                    SigInfoCode::Cld(cld_code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            Signal::SIGILL => {
                if let Ok(code) = SigInfoCodeIll::try_from(raw_code) {
                    SigInfoCode::Ill(code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            Signal::SIGFPE => {
                if let Ok(code) = SigInfoCodeFpe::try_from(raw_code) {
                    SigInfoCode::Fpe(code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            Signal::SIGSEGV => {
                if let Ok(code) = SigInfoCodeSegv::try_from(raw_code) {
                    SigInfoCode::Segv(code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            Signal::SIGBUS => {
                if let Ok(code) = SigInfoCodeBus::try_from(raw_code) {
                    SigInfoCode::Bus(code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            Signal::SIGTRAP => {
                if let Ok(code) = SigInfoCodeTrap::try_from(raw_code) {
                    SigInfoCode::Trap(code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            Signal::SIGPOLL => {
                if let Ok(code) = SigInfoCodePoll::try_from(raw_code) {
                    SigInfoCode::Poll(code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            Signal::SIGSYS => {
                if let Ok(code) = SigInfoCodeSys::try_from(raw_code) {
                    SigInfoCode::Sys(code)
                } else {
                    SigInfoCode::Unknown(raw_code)
                }
            }
            _ => unimplemented!(),
        }
    }

    pub fn details(&self) -> Option<SigInfoDetails> {
        match self.code() {
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
            SigInfoCode::Unknown(_) => None,
        }
    }

    pub fn new_for_kill(signal: Signal, sender_pid: i32, sender_uid: u32) -> Self {
        // sigaction(2):
        // > Signals  sent  with  kill(2) and sigqueue(3) fill in si_pid and si_uid.
        // > In addition, signals sent with sigqueue(3) fill in si_int and si_ptr with
        // > the values specified by the sender of the signal; see sigqueue(3) for
        // > more details.
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: signal.into(),
                    lsi_errno: 0,
                    lsi_code: SigInfoCodeSi::SI_USER.into(),
                    l_sifields: bindings::linux__sifields {
                        l_kill: SigInfoDetailsKill {
                            l_pid: sender_pid,
                            l_uid: sender_uid,
                        },
                    },
                },
            },
        })
    }

    pub fn new_for_tkill(signal: Signal, sender_pid: i32, sender_uid: u32) -> Self {
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: signal.into(),
                    lsi_errno: 0,
                    lsi_code: SigInfoCodeSi::SI_TKILL.into(),
                    l_sifields: bindings::linux__sifields {
                        l_kill: SigInfoDetailsKill {
                            l_pid: sender_pid,
                            l_uid: sender_uid,
                        },
                    },
                },
            },
        })
    }

    pub fn new_for_timer(signal: Signal, timer_id: i32, overrun: i32) -> Self {
        // sigaction(2):
        // > Signals sent by POSIX.1b timers (since Linux 2.6) fill in si_overrun and
        // > si_timerid.  The si_timerid field is  an  internal ID  used by the kernel
        // > to identify the timer; it is not the same as the timer ID returned by
        // > timer_create(2).  The si_overrun field is the timer overrun count; this
        // > is the same information as is obtained by a call to timer_getoverrun(2).
        // > These fields are nonstandard Linux extensions.
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: signal.into(),
                    lsi_errno: 0,
                    lsi_code: SigInfoCodeSi::SI_TIMER.into(),
                    l_sifields: bindings::linux__sifields {
                        l_timer: SigInfoDetailsTimer {
                            l_tid: timer_id,
                            l_overrun: overrun,
                            l_sigval: unsafe { core::mem::zeroed() },
                            l_sys_private: 0,
                        },
                    },
                },
            },
        })
    }

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
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: signal.into(),
                    lsi_errno: 0,
                    lsi_code: SigInfoCodeSi::SI_MESGQ.into(),
                    l_sifields: bindings::linux__sifields {
                        l_rt: SigInfoDetailsRt {
                            l_pid: sender_pid,
                            l_uid: sender_uid,
                            l_sigval: sigval,
                        },
                    },
                },
            },
        })
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
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: Signal::SIGCHLD.into(),
                    lsi_errno: 0,
                    lsi_code: SigInfoCodeCld::CLD_EXITED.into(),
                    l_sifields: bindings::linux__sifields {
                        l_sigchld: SigInfoDetailsSigChld {
                            l_pid: child_pid,
                            l_uid: child_uid,
                            l_status: child_exit_status,
                            l_utime: child_utime,
                            l_stime: child_stime,
                        },
                    },
                },
            },
        })
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
        Self(bindings::linux_siginfo_t {
            l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1 {
                l__bindgen_anon_1: bindings::linux_siginfo__bindgen_ty_1__bindgen_ty_1 {
                    lsi_signo: Signal::SIGCHLD.into(),
                    lsi_errno: 0,
                    lsi_code: code.into(),
                    l_sifields: bindings::linux__sifields {
                        l_sigchld: SigInfoDetailsSigChld {
                            l_pid: child_pid,
                            l_uid: child_uid,
                            l_status: signal.into(),
                            l_utime: child_utime,
                            l_stime: child_stime,
                        },
                    },
                },
            },
        })
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
    pub extern "C" fn linux_siginfo_new_for_kill(
        lsi_signo: i32,
        sender_pid: i32,
        sender_uid: u32,
    ) -> linux_siginfo_t {
        let signal = Signal::try_from(lsi_signo).unwrap();
        SigInfo::peel(SigInfo::new_for_kill(signal, sender_pid, sender_uid))
    }

    #[no_mangle]
    pub extern "C" fn linux_siginfo_new_for_tkill(
        lsi_signo: i32,
        sender_pid: i32,
        sender_uid: u32,
    ) -> linux_siginfo_t {
        let signal = Signal::try_from(lsi_signo).unwrap();
        SigInfo::peel(SigInfo::new_for_tkill(signal, sender_pid, sender_uid))
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
