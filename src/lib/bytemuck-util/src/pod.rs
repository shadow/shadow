use std::mem::MaybeUninit;

/// Marker trait that any bit pattern is a valid member of this type.
///
/// This is notably *not* true for many Rust types. e.g. interpreting the integer
/// value `2` as a rust `bool` is undefined behavior.
///
/// We require `Copy` to also rule out anything that implements `Drop`.
///
/// References are inherently non-Pod, so we can require a 'static lifetime.
///
/// We created this trait before learning about `bytemuck::AnyBitPattern`, which
/// is the same concept. Eventually we should *replace* this with
/// `bytemuck::AnyBitPattern`, but first we need to migrate our syscall handler
/// usage from libc types (which we don't own) to `linux-api` (kernel) types
/// (which we do), so that we can implement `bytemuck::AnyBitPattern` for them.
///
/// # Safety
///
/// Any pattern of bits must be a valid value of the given type.
pub unsafe trait AnyBitPattern: Sized + Copy + 'static {}

/// Convert to a slice of raw bytes.
///
/// Some bytes may be uninitialized if T has padding.
pub fn maybeuninit_bytes_of_slice<T>(slice: &[T]) -> &[MaybeUninit<u8>]
where
    T: AnyBitPattern,
{
    // SAFETY: Any value and alignment is safe for u8.
    unsafe {
        std::slice::from_raw_parts(
            slice.as_ptr() as *const MaybeUninit<u8>,
            slice.len() * std::mem::size_of::<MaybeUninit<T>>(),
        )
    }
}

/// Cast as a slice of raw bytes.
///
/// Analogous to `bytemuck::bytes_of`, but only requires `AnyBitPattern` instead
/// of `Pod`.  Some bytes may be uninitialized if T has padding.
// TODO: Do we even need to require `T: AnyBitPattern` here?
pub fn maybeuninit_bytes_of<T>(x: &T) -> &[MaybeUninit<u8>]
where
    T: AnyBitPattern,
{
    maybeuninit_bytes_of_slice(std::slice::from_ref(x))
}

/// Convert to a mut slice of raw bytes.
///
/// Some bytes may be uninialized if T has padding.
///
/// # Safety
///
/// Uninitialized bytes (e.g. [`MaybeUninit::uninit`]) must not be written
/// into the returned slice, which would invalidate the source `slice`.
pub unsafe fn maybeuninit_bytes_of_slice_mut<T>(slice: &mut [T]) -> &mut [MaybeUninit<u8>]
where
    T: AnyBitPattern,
{
    // SAFETY: Any value and alignment is safe for u8.
    unsafe {
        std::slice::from_raw_parts_mut(
            slice.as_mut_ptr() as *mut MaybeUninit<u8>,
            slice.len() * std::mem::size_of::<MaybeUninit<T>>(),
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
    T: AnyBitPattern,
{
    unsafe { maybeuninit_bytes_of_slice_mut(std::slice::from_mut(x)) }
}

/// Create a value of type `T`, with contents initialized to 0s.
pub fn zeroed<T>() -> T
where
    T: AnyBitPattern,
{
    // SAFETY: Any value is legal for Pod.
    unsafe { std::mem::zeroed() }
}

// Integer primitives
unsafe impl AnyBitPattern for u8 {}
unsafe impl AnyBitPattern for u16 {}
unsafe impl AnyBitPattern for u32 {}
unsafe impl AnyBitPattern for u64 {}
unsafe impl AnyBitPattern for i8 {}
unsafe impl AnyBitPattern for i16 {}
unsafe impl AnyBitPattern for i32 {}
unsafe impl AnyBitPattern for i64 {}
unsafe impl AnyBitPattern for isize {}
unsafe impl AnyBitPattern for usize {}

// No! Values other than 0 or 1 are invalid.
// impl !Pod for bool {}

// No! `char` must be a valid unicode value.
// impl !Pod for char {}

unsafe impl<T> AnyBitPattern for std::mem::MaybeUninit<T> where T: AnyBitPattern {}
unsafe impl<T, const N: usize> AnyBitPattern for [T; N] where T: AnyBitPattern {}

// libc types
unsafe impl AnyBitPattern for libc::Dl_info {}
unsafe impl AnyBitPattern for libc::Elf32_Chdr {}
unsafe impl AnyBitPattern for libc::Elf32_Ehdr {}
unsafe impl AnyBitPattern for libc::Elf32_Phdr {}
unsafe impl AnyBitPattern for libc::Elf32_Shdr {}
unsafe impl AnyBitPattern for libc::Elf32_Sym {}
unsafe impl AnyBitPattern for libc::Elf64_Chdr {}
unsafe impl AnyBitPattern for libc::Elf64_Ehdr {}
unsafe impl AnyBitPattern for libc::Elf64_Phdr {}
unsafe impl AnyBitPattern for libc::Elf64_Shdr {}
unsafe impl AnyBitPattern for libc::Elf64_Sym {}
unsafe impl AnyBitPattern for libc::__c_anonymous_sockaddr_can_j1939 {}
unsafe impl AnyBitPattern for libc::__c_anonymous_sockaddr_can_tp {}
unsafe impl AnyBitPattern for libc::__exit_status {}
unsafe impl AnyBitPattern for libc::__timeval {}
unsafe impl AnyBitPattern for libc::_libc_fpstate {}
unsafe impl AnyBitPattern for libc::_libc_fpxreg {}
unsafe impl AnyBitPattern for libc::_libc_xmmreg {}
unsafe impl AnyBitPattern for libc::addrinfo {}
//unsafe impl Pod for libc::af_alg_i {}
unsafe impl AnyBitPattern for libc::aiocb {}
unsafe impl AnyBitPattern for libc::arpd_request {}
unsafe impl AnyBitPattern for libc::arphdr {}
unsafe impl AnyBitPattern for libc::arpreq {}
unsafe impl AnyBitPattern for libc::arpreq_old {}
unsafe impl AnyBitPattern for libc::can_filter {}
unsafe impl AnyBitPattern for libc::can_frame {}
unsafe impl AnyBitPattern for libc::canfd_frame {}
unsafe impl AnyBitPattern for libc::cmsghdr {}
unsafe impl AnyBitPattern for libc::cpu_set_t {}
unsafe impl AnyBitPattern for libc::dirent {}
unsafe impl AnyBitPattern for libc::dirent64 {}
unsafe impl AnyBitPattern for libc::dl_phdr_info {}
unsafe impl AnyBitPattern for libc::dqblk {}
unsafe impl AnyBitPattern for libc::epoll_event {}
unsafe impl AnyBitPattern for libc::fanotify_event_metadata {}
unsafe impl AnyBitPattern for libc::fanotify_response {}
unsafe impl AnyBitPattern for libc::fd_set {}
unsafe impl AnyBitPattern for libc::ff_condition_effect {}
unsafe impl AnyBitPattern for libc::ff_constant_effect {}
unsafe impl AnyBitPattern for libc::ff_effect {}
unsafe impl AnyBitPattern for libc::ff_envelope {}
unsafe impl AnyBitPattern for libc::ff_periodic_effect {}
unsafe impl AnyBitPattern for libc::ff_ramp_effect {}
unsafe impl AnyBitPattern for libc::ff_replay {}
unsafe impl AnyBitPattern for libc::ff_rumble_effect {}
unsafe impl AnyBitPattern for libc::ff_trigger {}
unsafe impl AnyBitPattern for libc::flock {}
unsafe impl AnyBitPattern for libc::flock64 {}
unsafe impl AnyBitPattern for libc::fsid_t {}
unsafe impl AnyBitPattern for libc::genlmsghdr {}
unsafe impl AnyBitPattern for libc::glob64_t {}
unsafe impl AnyBitPattern for libc::glob_t {}
unsafe impl AnyBitPattern for libc::group {}
unsafe impl AnyBitPattern for libc::hostent {}
unsafe impl AnyBitPattern for libc::if_nameindex {}
unsafe impl AnyBitPattern for libc::ifaddrs {}
unsafe impl AnyBitPattern for libc::in6_addr {}
unsafe impl AnyBitPattern for libc::in6_pktinfo {}
unsafe impl AnyBitPattern for libc::in6_rtmsg {}
unsafe impl AnyBitPattern for libc::in_addr {}
unsafe impl AnyBitPattern for libc::in_pktinfo {}
unsafe impl AnyBitPattern for libc::inotify_event {}
unsafe impl AnyBitPattern for libc::input_absinfo {}
unsafe impl AnyBitPattern for libc::input_event {}
unsafe impl AnyBitPattern for libc::input_id {}
unsafe impl AnyBitPattern for libc::input_keymap_entry {}
unsafe impl AnyBitPattern for libc::input_mask {}
unsafe impl AnyBitPattern for libc::iovec {}
unsafe impl AnyBitPattern for libc::ip_mreq {}
unsafe impl AnyBitPattern for libc::ip_mreq_source {}
unsafe impl AnyBitPattern for libc::ip_mreqn {}
unsafe impl AnyBitPattern for libc::ipc_perm {}
unsafe impl AnyBitPattern for libc::ipv6_mreq {}
unsafe impl AnyBitPattern for libc::itimerspec {}
unsafe impl AnyBitPattern for libc::itimerval {}
unsafe impl AnyBitPattern for libc::lconv {}
unsafe impl AnyBitPattern for libc::linger {}
unsafe impl AnyBitPattern for libc::mallinfo {}
unsafe impl AnyBitPattern for libc::max_align_t {}
unsafe impl AnyBitPattern for libc::mcontext_t {}
unsafe impl AnyBitPattern for libc::mmsghdr {}
unsafe impl AnyBitPattern for libc::mntent {}
unsafe impl AnyBitPattern for libc::mq_attr {}
unsafe impl AnyBitPattern for libc::msghdr {}
unsafe impl AnyBitPattern for libc::msginfo {}
unsafe impl AnyBitPattern for libc::msqid_ds {}
unsafe impl AnyBitPattern for libc::nl_mmap_hdr {}
unsafe impl AnyBitPattern for libc::nl_mmap_req {}
unsafe impl AnyBitPattern for libc::nl_pktinfo {}
unsafe impl AnyBitPattern for libc::nlattr {}
unsafe impl AnyBitPattern for libc::nlmsgerr {}
unsafe impl AnyBitPattern for libc::nlmsghdr {}
unsafe impl AnyBitPattern for libc::ntptimeval {}
unsafe impl AnyBitPattern for libc::packet_mreq {}
unsafe impl AnyBitPattern for libc::passwd {}
unsafe impl AnyBitPattern for libc::pollfd {}
unsafe impl AnyBitPattern for libc::posix_spawn_file_actions_t {}
unsafe impl AnyBitPattern for libc::posix_spawnattr_t {}
unsafe impl AnyBitPattern for libc::protoent {}
unsafe impl AnyBitPattern for libc::pthread_attr_t {}
unsafe impl AnyBitPattern for libc::pthread_cond_t {}
unsafe impl AnyBitPattern for libc::pthread_condattr_t {}
unsafe impl AnyBitPattern for libc::pthread_mutex_t {}
unsafe impl AnyBitPattern for libc::pthread_mutexattr_t {}
unsafe impl AnyBitPattern for libc::pthread_rwlock_t {}
unsafe impl AnyBitPattern for libc::pthread_rwlockattr_t {}
unsafe impl AnyBitPattern for libc::regex_t {}
unsafe impl AnyBitPattern for libc::regmatch_t {}
unsafe impl AnyBitPattern for libc::rlimit {}
unsafe impl AnyBitPattern for libc::rlimit64 {}
unsafe impl AnyBitPattern for libc::rtentry {}
unsafe impl AnyBitPattern for libc::rusage {}
unsafe impl AnyBitPattern for libc::sched_param {}
unsafe impl AnyBitPattern for libc::sem_t {}
unsafe impl AnyBitPattern for libc::sembuf {}
unsafe impl AnyBitPattern for libc::servent {}
unsafe impl AnyBitPattern for libc::shmid_ds {}
unsafe impl AnyBitPattern for libc::sigaction {}
unsafe impl AnyBitPattern for libc::sigevent {}
unsafe impl AnyBitPattern for libc::siginfo_t {}
unsafe impl AnyBitPattern for libc::signalfd_siginfo {}
unsafe impl AnyBitPattern for libc::sigset_t {}
unsafe impl AnyBitPattern for libc::sigval {}
unsafe impl AnyBitPattern for libc::sock_extended_err {}
unsafe impl AnyBitPattern for libc::sockaddr {}
unsafe impl AnyBitPattern for libc::sockaddr_alg {}
unsafe impl AnyBitPattern for libc::sockaddr_can {}
unsafe impl AnyBitPattern for libc::sockaddr_in {}
unsafe impl AnyBitPattern for libc::sockaddr_in6 {}
unsafe impl AnyBitPattern for libc::sockaddr_ll {}
unsafe impl AnyBitPattern for libc::sockaddr_nl {}
unsafe impl AnyBitPattern for libc::sockaddr_storage {}
unsafe impl AnyBitPattern for libc::sockaddr_un {}
unsafe impl AnyBitPattern for libc::sockaddr_vm {}
unsafe impl AnyBitPattern for libc::spwd {}
unsafe impl AnyBitPattern for libc::stack_t {}
unsafe impl AnyBitPattern for libc::stat {}
unsafe impl AnyBitPattern for libc::stat64 {}
unsafe impl AnyBitPattern for libc::statfs {}
unsafe impl AnyBitPattern for libc::statfs64 {}
unsafe impl AnyBitPattern for libc::statvfs {}
unsafe impl AnyBitPattern for libc::statvfs64 {}
unsafe impl AnyBitPattern for libc::statx {}
unsafe impl AnyBitPattern for libc::statx_timestamp {}
unsafe impl AnyBitPattern for libc::sysinfo {}
unsafe impl AnyBitPattern for libc::termios {}
unsafe impl AnyBitPattern for libc::termios2 {}
unsafe impl AnyBitPattern for libc::timespec {}
unsafe impl AnyBitPattern for libc::timeval {}
unsafe impl AnyBitPattern for libc::timex {}
unsafe impl AnyBitPattern for libc::tm {}
unsafe impl AnyBitPattern for libc::tms {}
unsafe impl AnyBitPattern for libc::ucontext_t {}
unsafe impl AnyBitPattern for libc::ucred {}
unsafe impl AnyBitPattern for libc::uinput_abs_setup {}
unsafe impl AnyBitPattern for libc::uinput_ff_erase {}
unsafe impl AnyBitPattern for libc::uinput_ff_upload {}
unsafe impl AnyBitPattern for libc::uinput_setup {}
unsafe impl AnyBitPattern for libc::uinput_user_dev {}
unsafe impl AnyBitPattern for libc::user {}
unsafe impl AnyBitPattern for libc::user_fpregs_struct {}
unsafe impl AnyBitPattern for libc::user_regs_struct {}
unsafe impl AnyBitPattern for libc::utimbuf {}
unsafe impl AnyBitPattern for libc::utmpx {}
unsafe impl AnyBitPattern for libc::utsname {}
unsafe impl AnyBitPattern for libc::winsize {}
unsafe impl AnyBitPattern for libc::clone_args {}
