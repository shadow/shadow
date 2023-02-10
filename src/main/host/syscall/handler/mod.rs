use std::mem::MaybeUninit;

use crate::cshadow as c;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::descriptor_table::{DescriptorHandle, DescriptorTable};
use crate::host::descriptor::Descriptor;
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{
    PluginPtr, SysCallArgs, SysCallReg, SyscallError, SyscallResult, TypedPluginPtr,
};
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::{pod, NoTypeInference};

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

    pub fn syscall(&self, mut ctx: SyscallContext) -> SyscallResult {
        match ctx.args.number {
            libc::SYS_accept => SyscallHandlerFn::call(Self::accept, &mut ctx),
            libc::SYS_accept4 => SyscallHandlerFn::call(Self::accept4, &mut ctx),
            libc::SYS_bind => SyscallHandlerFn::call(Self::bind, &mut ctx),
            libc::SYS_brk => SyscallHandlerFn::call(Self::brk, &mut ctx),
            libc::SYS_close => SyscallHandlerFn::call(Self::close, &mut ctx),
            libc::SYS_connect => SyscallHandlerFn::call(Self::connect, &mut ctx),
            libc::SYS_dup => SyscallHandlerFn::call(Self::dup, &mut ctx),
            libc::SYS_dup2 => SyscallHandlerFn::call(Self::dup2, &mut ctx),
            libc::SYS_dup3 => SyscallHandlerFn::call(Self::dup3, &mut ctx),
            libc::SYS_eventfd => SyscallHandlerFn::call(Self::eventfd, &mut ctx),
            libc::SYS_eventfd2 => SyscallHandlerFn::call(Self::eventfd2, &mut ctx),
            libc::SYS_fcntl => SyscallHandlerFn::call(Self::fcntl, &mut ctx),
            libc::SYS_getitimer => SyscallHandlerFn::call(Self::getitimer, &mut ctx),
            libc::SYS_getpeername => SyscallHandlerFn::call(Self::getpeername, &mut ctx),
            libc::SYS_getrandom => SyscallHandlerFn::call(Self::getrandom, &mut ctx),
            libc::SYS_getsockname => SyscallHandlerFn::call(Self::getsockname, &mut ctx),
            libc::SYS_getsockopt => SyscallHandlerFn::call(Self::getsockopt, &mut ctx),
            libc::SYS_ioctl => SyscallHandlerFn::call(Self::ioctl, &mut ctx),
            libc::SYS_listen => SyscallHandlerFn::call(Self::listen, &mut ctx),
            libc::SYS_mmap => SyscallHandlerFn::call(Self::mmap, &mut ctx),
            libc::SYS_mprotect => SyscallHandlerFn::call(Self::mprotect, &mut ctx),
            libc::SYS_mremap => SyscallHandlerFn::call(Self::mremap, &mut ctx),
            libc::SYS_munmap => SyscallHandlerFn::call(Self::munmap, &mut ctx),
            libc::SYS_open => SyscallHandlerFn::call(Self::open, &mut ctx),
            libc::SYS_openat => SyscallHandlerFn::call(Self::openat, &mut ctx),
            libc::SYS_pipe => SyscallHandlerFn::call(Self::pipe, &mut ctx),
            libc::SYS_pipe2 => SyscallHandlerFn::call(Self::pipe2, &mut ctx),
            libc::SYS_pread64 => SyscallHandlerFn::call(Self::pread64, &mut ctx),
            libc::SYS_pwrite64 => SyscallHandlerFn::call(Self::pwrite64, &mut ctx),
            libc::SYS_rseq => SyscallHandlerFn::call(Self::rseq, &mut ctx),
            libc::SYS_read => SyscallHandlerFn::call(Self::read, &mut ctx),
            libc::SYS_recvfrom => SyscallHandlerFn::call(Self::recvfrom, &mut ctx),
            libc::SYS_sched_getaffinity => {
                SyscallHandlerFn::call(Self::sched_getaffinity, &mut ctx)
            }
            libc::SYS_sched_setaffinity => {
                SyscallHandlerFn::call(Self::sched_setaffinity, &mut ctx)
            }
            libc::SYS_sched_yield => SyscallHandlerFn::call(Self::sched_yield, &mut ctx),
            libc::SYS_sendto => SyscallHandlerFn::call(Self::sendto, &mut ctx),
            libc::SYS_setitimer => SyscallHandlerFn::call(Self::setitimer, &mut ctx),
            libc::SYS_setsockopt => SyscallHandlerFn::call(Self::setsockopt, &mut ctx),
            libc::SYS_shutdown => SyscallHandlerFn::call(Self::shutdown, &mut ctx),
            libc::SYS_socket => SyscallHandlerFn::call(Self::socket, &mut ctx),
            libc::SYS_socketpair => SyscallHandlerFn::call(Self::socketpair, &mut ctx),
            libc::SYS_sysinfo => SyscallHandlerFn::call(Self::sysinfo, &mut ctx),
            libc::SYS_write => SyscallHandlerFn::call(Self::write, &mut ctx),
            _ => {
                // if we added a HANDLE_RUST() macro for this syscall in
                // 'syscallhandler_make_syscall()' but didn't add an entry here, we should get a
                // warning
                log::warn!("Rust syscall {} is not mapped", ctx.args.number);
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
    fn legacy_syscall(syscall: LegacySyscallFn, ctx: &mut SyscallContext) -> SyscallResult {
        unsafe { syscall(ctx.objs.thread.csyscallhandler(), ctx.args as *const _) }.into()
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

/// Writes `val` to `val_ptr`, but will only write a partial value if `val_len` is smaller than the
/// size of `val`. Returns the number of bytes written.
///
/// The generic type must be given explicitly to prevent accidentally writing the wrong type.
///
/// ```ignore
/// let bytes_written = write_partial::<i32, _>(mem, foo(), ptr, len)?;
/// ```
pub fn write_partial<U: NoTypeInference<This = T>, T: pod::Pod>(
    mem: &mut MemoryManager,
    val: &T,
    val_ptr: PluginPtr,
    val_len: usize,
) -> Result<usize, SyscallError> {
    let val_len = std::cmp::min(val_len, std::mem::size_of_val(val));

    let val = &pod::as_u8_slice(val)[..val_len];
    let val_ptr = TypedPluginPtr::new::<MaybeUninit<u8>>(val_ptr, val_len);

    mem.copy_to_ptr(val_ptr, val)?;

    Ok(val_len)
}

pub struct SyscallContext<'a, 'b> {
    pub objs: &'a mut ThreadContext<'b>,
    pub args: &'a SysCallArgs,
}

pub trait SyscallHandlerFn<T> {
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult;
}

impl<F> SyscallHandlerFn<()> for F
where
    F: Fn(&mut SyscallContext) -> SyscallResult,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(ctx)
    }
}

impl<F, T> SyscallHandlerFn<(T,)> for F
where
    F: Fn(&mut SyscallContext, T) -> SyscallResult,
    T: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        (self)(ctx, ctx.args.get(0).into())
    }
}

impl<F, T1, T2> SyscallHandlerFn<(T1, T2)> for F
where
    F: Fn(&mut SyscallContext, T1, T2) -> SyscallResult,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        (self)(ctx, ctx.args.get(0).into(), ctx.args.get(1).into())
    }
}

impl<F, T1, T2, T3> SyscallHandlerFn<(T1, T2, T3)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3) -> SyscallResult,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        (self)(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
        )
    }
}

impl<F, T1, T2, T3, T4> SyscallHandlerFn<(T1, T2, T3, T4)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3, T4) -> SyscallResult,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
    T4: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        (self)(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
            ctx.args.get(3).into(),
        )
    }
}

impl<F, T1, T2, T3, T4, T5> SyscallHandlerFn<(T1, T2, T3, T4, T5)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3, T4, T5) -> SyscallResult,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
    T4: From<SysCallReg>,
    T5: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        (self)(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
            ctx.args.get(3).into(),
            ctx.args.get(4).into(),
        )
    }
}

impl<F, T1, T2, T3, T4, T5, T6> SyscallHandlerFn<(T1, T2, T3, T4, T5, T6)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3, T4, T5, T6) -> SyscallResult,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
    T4: From<SysCallReg>,
    T5: From<SysCallReg>,
    T6: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        (self)(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
            ctx.args.get(3).into(),
            ctx.args.get(4).into(),
            ctx.args.get(5).into(),
        )
    }
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
            objs.with_ctx(|ctx| {
                let ctx = SyscallContext {
                    objs: ctx,
                    args: unsafe { args.as_ref().unwrap() },
                };
                sys.syscall(ctx).into()
            })
        })
        .unwrap()
    }
}
