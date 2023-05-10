use linux_kernel_types::signal::Signal;

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
pub use linux_kernel_types::signal::linux_sigset_t;

/*
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
*/

pub use linux_kernel_types::signal::linux_sigaction;

mod export {
    use linux_kernel_types::signal::DefaultAction;

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
    pub extern "C" fn linux_defaultAction(signo: i32) -> DefaultAction {
        let sig = Signal::try_from(signo).unwrap();
        linux_kernel_types::signal::defaultaction(sig)
    }
}
