//! Utilities for working with POD (Plain Old Data)

#![no_std]
// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

use core::mem::MaybeUninit;

/// Marker trait that the given type is Plain Old Data; i.e. that it is safe to
/// interpret any pattern of bits as a value of this type.
///
/// This is notably *not* true for many Rust types. e.g. interpreting the integer
/// value `2` as a rust `bool` is undefined behavior.
///
/// We require `Copy` to also rule out anything that implements `Drop`.
///
/// References are inherently non-Pod, so we can require a 'static lifetime.
///
/// This is very *similar* in concept to `bytemuck::AnyBitPattern`. However,
/// unlike `AnyBitPattern`, this trait does not say anything about how the type
/// can be safely shared. e.g. while `bytemuck::AnyBitPattern` disallows pointer
/// types, [`Pod`] does not.
///
/// # Safety
///
/// - Any pattern of bits must be a valid value of the given type.
/// - The type must not contain an [`UnsafeCell`](core::cell::UnsafeCell), or any other structure
///   that contains an `UnsafeCell` (for example [`Cell`](core::cell::Cell)). Otherwise the following
///   code would have UB:
///   ```ignore
///   let x = Cell::new(0);
///   let y = as_u8_slice(&x);
///   x.set(1);
///   ```
pub unsafe trait Pod: Copy + 'static {}

/// Convert to a slice of raw bytes.
///
/// Some bytes may be uninitialized if T has padding.
pub fn to_u8_slice<T>(slice: &[T]) -> &[MaybeUninit<u8>]
where
    T: Pod,
{
    // SAFETY: Any value and alignment is safe for u8.
    unsafe {
        core::slice::from_raw_parts(
            slice.as_ptr() as *const MaybeUninit<u8>,
            slice.len() * core::mem::size_of::<MaybeUninit<T>>(),
        )
    }
}

/// Cast as a slice of raw bytes.
///
/// Some bytes may be uninitialized if T has padding.
pub fn as_u8_slice<T>(x: &T) -> &[MaybeUninit<u8>]
where
    T: Pod,
{
    to_u8_slice(core::slice::from_ref(x))
}

/// Convert to a mut slice of raw bytes.
///
/// Some bytes may be uninialized if T has padding.
///
/// # Safety
///
/// Uninitialized bytes (e.g. [`MaybeUninit::uninit`]) must not be written
/// into the returned slice, which would invalidate the source `slice`.
pub unsafe fn to_u8_slice_mut<T>(slice: &mut [T]) -> &mut [MaybeUninit<u8>]
where
    T: Pod,
{
    // SAFETY: Any value and alignment is safe for u8.
    unsafe {
        core::slice::from_raw_parts_mut(
            slice.as_mut_ptr() as *mut MaybeUninit<u8>,
            slice.len() * core::mem::size_of::<MaybeUninit<T>>(),
        )
    }
}

/// Cast as a mut slice of raw bytes.
///
/// Some bytes may be uninitialized if T has padding.
///
/// # Safety
///
/// See [`to_u8_slice_mut`].
pub unsafe fn as_u8_slice_mut<T>(x: &mut T) -> &mut [MaybeUninit<u8>]
where
    T: Pod,
{
    unsafe { to_u8_slice_mut(core::slice::from_mut(x)) }
}

/// Create a value of type `T`, with contents initialized to 0s.
pub fn zeroed<T>() -> T
where
    T: Pod,
{
    // SAFETY: Any value is legal for Pod.
    unsafe { core::mem::zeroed() }
}

/// Wrapper type to support associated compile-time size checks
struct PodTransmute<const N: usize, T> {
    _t: core::marker::PhantomData<T>,
}

impl<const N: usize, T: Pod> PodTransmute<N, T> {
    const CHECK: () = assert!(N == core::mem::size_of::<T>());
    #[inline(always)]
    fn transmute_array(x: &[u8; N]) -> T {
        // this should perform a compile-time check
        #[allow(clippy::let_unit_value)]
        let _ = Self::CHECK;

        // this should perform a runtime check in case the above compile-time check didn't run, but
        // should be compiled out if the compile-time check did run
        assert_eq!(N, core::mem::size_of::<T>());

        // It'd be nice to use `transmute` here, and take the array by value,
        // but there's no way to convince the type system that the input and output
        // sizes are guaranteed to be equal. So, we use `transmute_copy` which
        // doesn't require this to be statically guaranteed.
        unsafe { core::mem::transmute_copy(x) }
    }
}

/// Interpret the bytes of `x` as a value of type `T`.
pub fn from_array<const N: usize, T: Pod>(x: &[u8; N]) -> T {
    PodTransmute::transmute_array(x)
}

// Integer primitives
unsafe impl Pod for u8 {}
unsafe impl Pod for u16 {}
unsafe impl Pod for u32 {}
unsafe impl Pod for u64 {}
unsafe impl Pod for i8 {}
unsafe impl Pod for i16 {}
unsafe impl Pod for i32 {}
unsafe impl Pod for i64 {}
unsafe impl Pod for isize {}
unsafe impl Pod for usize {}

// No! Values other than 0 or 1 are invalid.
// impl !Pod for bool {}

// No! `char` must be a valid unicode value.
// impl !Pod for char {}

unsafe impl<T> Pod for core::mem::MaybeUninit<T> where T: Pod {}
unsafe impl<T, const N: usize> Pod for [T; N] where T: Pod {}

// libc types
unsafe impl Pod for libc::Dl_info {}
unsafe impl Pod for libc::Elf32_Chdr {}
unsafe impl Pod for libc::Elf32_Ehdr {}
unsafe impl Pod for libc::Elf32_Phdr {}
unsafe impl Pod for libc::Elf32_Shdr {}
unsafe impl Pod for libc::Elf32_Sym {}
unsafe impl Pod for libc::Elf64_Chdr {}
unsafe impl Pod for libc::Elf64_Ehdr {}
unsafe impl Pod for libc::Elf64_Phdr {}
unsafe impl Pod for libc::Elf64_Shdr {}
unsafe impl Pod for libc::Elf64_Sym {}
unsafe impl Pod for libc::__c_anonymous_sockaddr_can_j1939 {}
unsafe impl Pod for libc::__c_anonymous_sockaddr_can_tp {}
unsafe impl Pod for libc::__exit_status {}
unsafe impl Pod for libc::__timeval {}
unsafe impl Pod for libc::_libc_fpstate {}
unsafe impl Pod for libc::_libc_fpxreg {}
unsafe impl Pod for libc::_libc_xmmreg {}
unsafe impl Pod for libc::addrinfo {}
//unsafe impl Pod for libc::af_alg_i {}
unsafe impl Pod for libc::aiocb {}
unsafe impl Pod for libc::arpd_request {}
unsafe impl Pod for libc::arphdr {}
unsafe impl Pod for libc::arpreq {}
unsafe impl Pod for libc::arpreq_old {}
unsafe impl Pod for libc::can_filter {}
unsafe impl Pod for libc::can_frame {}
unsafe impl Pod for libc::canfd_frame {}
unsafe impl Pod for libc::cmsghdr {}
unsafe impl Pod for libc::cpu_set_t {}
unsafe impl Pod for libc::dirent {}
unsafe impl Pod for libc::dirent64 {}
unsafe impl Pod for libc::dl_phdr_info {}
unsafe impl Pod for libc::dqblk {}
unsafe impl Pod for libc::epoll_event {}
unsafe impl Pod for libc::fanotify_event_metadata {}
unsafe impl Pod for libc::fanotify_response {}
unsafe impl Pod for libc::fd_set {}
unsafe impl Pod for libc::ff_condition_effect {}
unsafe impl Pod for libc::ff_constant_effect {}
unsafe impl Pod for libc::ff_effect {}
unsafe impl Pod for libc::ff_envelope {}
unsafe impl Pod for libc::ff_periodic_effect {}
unsafe impl Pod for libc::ff_ramp_effect {}
unsafe impl Pod for libc::ff_replay {}
unsafe impl Pod for libc::ff_rumble_effect {}
unsafe impl Pod for libc::ff_trigger {}
unsafe impl Pod for libc::flock {}
unsafe impl Pod for libc::flock64 {}
unsafe impl Pod for libc::fsid_t {}
unsafe impl Pod for libc::genlmsghdr {}
unsafe impl Pod for libc::glob64_t {}
unsafe impl Pod for libc::glob_t {}
unsafe impl Pod for libc::group {}
unsafe impl Pod for libc::hostent {}
unsafe impl Pod for libc::if_nameindex {}
unsafe impl Pod for libc::ifaddrs {}
unsafe impl Pod for libc::in6_addr {}
unsafe impl Pod for libc::in6_pktinfo {}
unsafe impl Pod for libc::in6_rtmsg {}
unsafe impl Pod for libc::in_addr {}
unsafe impl Pod for libc::in_pktinfo {}
unsafe impl Pod for libc::inotify_event {}
unsafe impl Pod for libc::input_absinfo {}
unsafe impl Pod for libc::input_event {}
unsafe impl Pod for libc::input_id {}
unsafe impl Pod for libc::input_keymap_entry {}
unsafe impl Pod for libc::input_mask {}
unsafe impl Pod for libc::iovec {}
unsafe impl Pod for libc::ip_mreq {}
unsafe impl Pod for libc::ip_mreq_source {}
unsafe impl Pod for libc::ip_mreqn {}
unsafe impl Pod for libc::ipc_perm {}
unsafe impl Pod for libc::ipv6_mreq {}
unsafe impl Pod for libc::itimerspec {}
unsafe impl Pod for libc::itimerval {}
unsafe impl Pod for libc::lconv {}
unsafe impl Pod for libc::linger {}
unsafe impl Pod for libc::mallinfo {}
unsafe impl Pod for libc::max_align_t {}
unsafe impl Pod for libc::mcontext_t {}
unsafe impl Pod for libc::mmsghdr {}
unsafe impl Pod for libc::mntent {}
unsafe impl Pod for libc::mq_attr {}
unsafe impl Pod for libc::msghdr {}
unsafe impl Pod for libc::msginfo {}
unsafe impl Pod for libc::msqid_ds {}
unsafe impl Pod for libc::nl_mmap_hdr {}
unsafe impl Pod for libc::nl_mmap_req {}
unsafe impl Pod for libc::nl_pktinfo {}
unsafe impl Pod for libc::nlattr {}
unsafe impl Pod for libc::nlmsgerr {}
unsafe impl Pod for libc::nlmsghdr {}
unsafe impl Pod for libc::ntptimeval {}
unsafe impl Pod for libc::packet_mreq {}
unsafe impl Pod for libc::passwd {}
unsafe impl Pod for libc::pollfd {}
unsafe impl Pod for libc::posix_spawn_file_actions_t {}
unsafe impl Pod for libc::posix_spawnattr_t {}
unsafe impl Pod for libc::protoent {}
unsafe impl Pod for libc::pthread_attr_t {}
unsafe impl Pod for libc::pthread_cond_t {}
unsafe impl Pod for libc::pthread_condattr_t {}
unsafe impl Pod for libc::pthread_mutex_t {}
unsafe impl Pod for libc::pthread_mutexattr_t {}
unsafe impl Pod for libc::pthread_rwlock_t {}
unsafe impl Pod for libc::pthread_rwlockattr_t {}
unsafe impl Pod for libc::regex_t {}
unsafe impl Pod for libc::regmatch_t {}
unsafe impl Pod for libc::rlimit {}
unsafe impl Pod for libc::rlimit64 {}
unsafe impl Pod for libc::rtentry {}
unsafe impl Pod for libc::rusage {}
unsafe impl Pod for libc::sched_param {}
unsafe impl Pod for libc::sem_t {}
unsafe impl Pod for libc::sembuf {}
unsafe impl Pod for libc::servent {}
unsafe impl Pod for libc::shmid_ds {}
unsafe impl Pod for libc::sigaction {}
unsafe impl Pod for libc::sigevent {}
unsafe impl Pod for libc::siginfo_t {}
unsafe impl Pod for libc::signalfd_siginfo {}
unsafe impl Pod for libc::sigset_t {}
unsafe impl Pod for libc::sigval {}
unsafe impl Pod for libc::sock_extended_err {}
unsafe impl Pod for libc::sockaddr {}
unsafe impl Pod for libc::sockaddr_alg {}
unsafe impl Pod for libc::sockaddr_can {}
unsafe impl Pod for libc::sockaddr_in {}
unsafe impl Pod for libc::sockaddr_in6 {}
unsafe impl Pod for libc::sockaddr_ll {}
unsafe impl Pod for libc::sockaddr_nl {}
unsafe impl Pod for libc::sockaddr_storage {}
unsafe impl Pod for libc::sockaddr_un {}
unsafe impl Pod for libc::sockaddr_vm {}
unsafe impl Pod for libc::spwd {}
unsafe impl Pod for libc::stack_t {}
unsafe impl Pod for libc::stat {}
unsafe impl Pod for libc::stat64 {}
unsafe impl Pod for libc::statfs {}
unsafe impl Pod for libc::statfs64 {}
unsafe impl Pod for libc::statvfs {}
unsafe impl Pod for libc::statvfs64 {}
unsafe impl Pod for libc::statx {}
unsafe impl Pod for libc::statx_timestamp {}
unsafe impl Pod for libc::sysinfo {}
unsafe impl Pod for libc::termios {}
unsafe impl Pod for libc::termios2 {}
unsafe impl Pod for libc::timespec {}
unsafe impl Pod for libc::timeval {}
unsafe impl Pod for libc::timex {}
unsafe impl Pod for libc::tm {}
unsafe impl Pod for libc::tms {}
unsafe impl Pod for libc::ucontext_t {}
unsafe impl Pod for libc::ucred {}
unsafe impl Pod for libc::uinput_abs_setup {}
unsafe impl Pod for libc::uinput_ff_erase {}
unsafe impl Pod for libc::uinput_ff_upload {}
unsafe impl Pod for libc::uinput_setup {}
unsafe impl Pod for libc::uinput_user_dev {}
unsafe impl Pod for libc::user {}
unsafe impl Pod for libc::user_fpregs_struct {}
unsafe impl Pod for libc::user_regs_struct {}
unsafe impl Pod for libc::utimbuf {}
unsafe impl Pod for libc::utmpx {}
unsafe impl Pod for libc::utsname {}
unsafe impl Pod for libc::winsize {}
unsafe impl Pod for libc::clone_args {}
