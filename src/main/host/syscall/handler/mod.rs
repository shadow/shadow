use std::mem::MaybeUninit;

use crate::cshadow as c;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::descriptor_table::{DescriptorHandle, DescriptorTable};
use crate::host::descriptor::Descriptor;
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{
    PluginPtr, SysCallArgs, SyscallError, SyscallResult, TypedPluginPtr,
};
use crate::utility::sockaddr::SockaddrStorage;

use nix::errno::Errno;

mod eventfd;
mod fcntl;
mod file;
mod ioctl;
mod mman;
mod random;
mod sched;
mod socket;
mod sysinfo;
mod time;
mod unistd;

type LegacySyscallFn =
    unsafe extern "C" fn(*mut c::SysCallHandler, *const c::SysCallArgs) -> c::SysCallReturn;

pub struct SyscallHandler {
    // Will eventually contain syscall handler state once migrated from the c handler
}

impl SyscallHandler {
    #[allow(clippy::new_without_default)]
    pub fn new() -> SyscallHandler {
        SyscallHandler {}
    }

    pub fn syscall(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        match args.number {
            libc::SYS_accept => Self::accept(ctx, args),
            libc::SYS_accept4 => Self::accept4(ctx, args),
            libc::SYS_bind => Self::bind(ctx, args),
            libc::SYS_brk => Self::brk(ctx, args),
            libc::SYS_close => Self::close(ctx, args),
            libc::SYS_connect => Self::connect(ctx, args),
            libc::SYS_dup => Self::dup(ctx, args),
            libc::SYS_dup2 => Self::dup2(ctx, args),
            libc::SYS_dup3 => Self::dup3(ctx, args),
            libc::SYS_eventfd => Self::eventfd(ctx, args),
            libc::SYS_eventfd2 => Self::eventfd2(ctx, args),
            libc::SYS_fcntl => Self::fcntl(ctx, args),
            libc::SYS_getitimer => Self::getitimer(ctx, args),
            libc::SYS_getpeername => Self::getpeername(ctx, args),
            libc::SYS_getrandom => Self::getrandom(ctx, args),
            libc::SYS_getsockname => Self::getsockname(ctx, args),
            libc::SYS_getsockopt => Self::getsockopt(ctx, args),
            libc::SYS_ioctl => Self::ioctl(ctx, args),
            libc::SYS_listen => Self::listen(ctx, args),
            libc::SYS_mmap => Self::mmap(ctx, args),
            libc::SYS_mprotect => Self::mprotect(ctx, args),
            libc::SYS_mremap => Self::mremap(ctx, args),
            libc::SYS_munmap => Self::munmap(ctx, args),
            libc::SYS_open => Self::open(ctx, args),
            libc::SYS_openat => Self::openat(ctx, args),
            libc::SYS_pipe => Self::pipe(ctx, args),
            libc::SYS_pipe2 => Self::pipe2(ctx, args),
            libc::SYS_pread64 => Self::pread64(ctx, args),
            libc::SYS_pwrite64 => Self::pwrite64(ctx, args),
            libc::SYS_rseq => Self::rseq(ctx, args),
            libc::SYS_read => Self::read(ctx, args),
            libc::SYS_recvfrom => Self::recvfrom(ctx, args),
            libc::SYS_sched_getaffinity => Self::sched_getaffinity(ctx, args),
            libc::SYS_sched_setaffinity => Self::sched_setaffinity(ctx, args),
            libc::SYS_sched_yield => Self::sched_yield(ctx, args),
            libc::SYS_sendto => Self::sendto(ctx, args),
            libc::SYS_setitimer => Self::setitimer(ctx, args),
            libc::SYS_setsockopt => Self::setsockopt(ctx, args),
            libc::SYS_shutdown => Self::shutdown(ctx, args),
            libc::SYS_socket => Self::socket(ctx, args),
            libc::SYS_socketpair => Self::socketpair(ctx, args),
            libc::SYS_sysinfo => Self::sysinfo(ctx, args),
            libc::SYS_write => Self::write(ctx, args),
            _ => {
                // if we added a HANDLE_RUST() macro for this syscall in
                // 'syscallhandler_make_syscall()' but didn't add an entry here, we should get a
                // warning
                log::warn!("Rust syscall {} is not mapped", args.number);
                Err(Errno::ENOSYS.into())
            }
        }
    }

    /// Internal helper that returns the `Descriptor` for the fd if it exists, otherwise returns
    /// EBADF.
    fn get_descriptor(
        descriptor_table: &DescriptorTable,
        fd: impl TryInto<DescriptorHandle>,
    ) -> Result<&Descriptor, nix::errno::Errno> {
        // check that fd is within bounds
        let fd = fd.try_into().or(Err(nix::errno::Errno::EBADF))?;

        match descriptor_table.get(fd) {
            Some(desc) => Ok(desc),
            None => Err(nix::errno::Errno::EBADF),
        }
    }

    /// Internal helper that returns the `Descriptor` for the fd if it exists, otherwise returns
    /// EBADF.
    fn get_descriptor_mut(
        descriptor_table: &mut DescriptorTable,
        fd: impl TryInto<DescriptorHandle>,
    ) -> Result<&mut Descriptor, nix::errno::Errno> {
        // check that fd is within bounds
        let fd = fd.try_into().or(Err(nix::errno::Errno::EBADF))?;

        match descriptor_table.get_mut(fd) {
            Some(desc) => Ok(desc),
            None => Err(nix::errno::Errno::EBADF),
        }
    }

    /// Run a legacy C syscall handler.
    fn legacy_syscall(
        syscall: LegacySyscallFn,
        ctx: &mut ThreadContext,
        args: &SysCallArgs,
    ) -> SyscallResult {
        unsafe { syscall(ctx.thread.csyscallhandler(), args as *const _) }.into()
    }
}

pub fn write_sockaddr(
    mem: &mut MemoryManager,
    addr: Option<&SockaddrStorage>,
    plugin_addr: PluginPtr,
    plugin_addr_len: TypedPluginPtr<libc::socklen_t>,
) -> Result<(), SyscallError> {
    let addr = match addr {
        Some(x) => x,
        None => {
            mem.copy_to_ptr(plugin_addr_len, &[0])?;
            return Ok(());
        }
    };

    let from_addr_slice = addr.as_slice();
    let from_len: u32 = from_addr_slice.len().try_into().unwrap();

    // get the provided address buffer length, and overwrite it with the real address length
    let plugin_addr_len = {
        let mut plugin_addr_len = mem.memory_ref_mut(plugin_addr_len)?;
        let plugin_addr_len_value = plugin_addr_len.get_mut(0).unwrap();

        // keep a copy before we change it
        let plugin_addr_len_copy = *plugin_addr_len_value;

        *plugin_addr_len_value = from_len;

        plugin_addr_len.flush()?;
        plugin_addr_len_copy
    };

    // return early if the address length is 0
    if plugin_addr_len == 0 {
        return Ok(());
    }

    // the minimum of the given address buffer length and the real address length
    let len_to_copy = std::cmp::min(from_len, plugin_addr_len).try_into().unwrap();

    let plugin_addr = TypedPluginPtr::new::<MaybeUninit<u8>>(plugin_addr, len_to_copy);
    mem.copy_to_ptr(plugin_addr, &from_addr_slice[..len_to_copy])?;

    Ok(())
}

pub fn read_sockaddr(
    mem: &MemoryManager,
    addr_ptr: PluginPtr,
    addr_len: libc::socklen_t,
) -> Result<Option<SockaddrStorage>, SyscallError> {
    if addr_ptr.is_null() {
        return Ok(None);
    }

    let addr_len_usize: usize = addr_len.try_into().unwrap();

    // this won't have the correct alignment, but that's fine since `SockaddrStorage::from_bytes()`
    // doesn't require alignment
    let mut addr_buf = [MaybeUninit::new(0u8); std::mem::size_of::<libc::sockaddr_storage>()];

    // make sure we will not lose data when we copy
    if addr_len_usize > std::mem::size_of_val(&addr_buf) {
        log::warn!(
            "Shadow does not support the address length {}, which is larger than {}",
            addr_len,
            std::mem::size_of_val(&addr_buf),
        );
        return Err(Errno::EINVAL.into());
    }

    let addr_buf = &mut addr_buf[..addr_len_usize];

    mem.copy_from_ptr(
        addr_buf,
        TypedPluginPtr::new::<MaybeUninit<u8>>(addr_ptr, addr_len_usize),
    )?;

    let addr = unsafe { SockaddrStorage::from_bytes(addr_buf).ok_or(Errno::EINVAL)? };

    Ok(Some(addr))
}

mod export {
    use shadow_shim_helper_rs::notnull::*;

    use crate::core::worker::Worker;

    use super::*;

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_new() -> *mut SyscallHandler {
        Box::into_raw(Box::new(SyscallHandler::new()))
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_free(handler_ptr: *mut SyscallHandler) {
        if handler_ptr.is_null() {
            return;
        }
        unsafe { Box::from_raw(handler_ptr) };
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_syscall(
        sys: *mut SyscallHandler,
        csys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null());
        let sys = unsafe { &mut *sys };
        Worker::with_active_host(|host| {
            let mut objs =
                unsafe { ThreadContextObjs::from_syscallhandler(host, notnull_mut_debug(csys)) };
            objs.with_ctx(|ctx| sys.syscall(ctx, unsafe { args.as_ref().unwrap() }).into())
        })
        .unwrap()
    }
}
