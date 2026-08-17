use std::ffi::CString;

use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::{SyscallSockAddrArg, SyscallStringArg};
use crate::host::syscall::types::ForeignArrayPtr;
use crate::utility::case_insensitive_eq;

impl SyscallHandler {
    log_syscall!(shadow_yield, /* rv */ std::ffi::c_int);
    pub fn shadow_yield(_ctx: &mut SyscallContext) -> Result<(), Errno> {
        Ok(())
    }

    log_syscall!(
        shadow_hostname_to_addr_ipv4,
        /* rv */ std::ffi::c_int,
        /* name_ptr */ SyscallStringArg,
        /* name_len */ u64,
        /* addr_ptr */ SyscallSockAddrArg<3>,
        /* addr_len */ u64,
    );
    pub fn shadow_hostname_to_addr_ipv4(
        ctx: &mut SyscallContext,
        name_ptr: ForeignPtr<std::ffi::c_char>,
        name_len: u64,
        addr_ptr: ForeignPtr<()>,
        addr_len: u64,
    ) -> Result<(), Errno> {
        log::trace!("Handling custom syscall shadow_hostname_to_addr_ipv4");

        let name_len: usize = name_len.try_into().unwrap();
        let addr_len: usize = addr_len.try_into().unwrap();

        if addr_len < std::mem::size_of::<u32>() {
            log::trace!("Invalid addr_len {addr_len}, returning EINVAL");
            return Err(Errno::EINVAL);
        }

        let name_ptr = name_ptr.cast::<u8>();
        let name_ptr = ForeignArrayPtr::new(name_ptr, name_len);
        let addr_ptr = addr_ptr.cast::<u32>();

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let name = mem.read_vec(name_ptr)?;
        let Ok(name) = CString::new(name) else {
            // name contained an internal (or trailing) nul byte.
            // treat as not-found.

            // Following existing comment to "return EFAULT like gethostname".
            return Err(Errno::EFAULT);
        };

        let addr = if case_insensitive_eq(name.as_bytes(), &b"localhost"[..]) {
            log::trace!("Returning loopback address for localhost");
            std::net::Ipv4Addr::LOCALHOST
        } else if case_insensitive_eq(name.as_bytes(), ctx.objs.host.info().name.as_bytes()) {
            log::trace!("Using default address for my own hostname {name:?}");
            ctx.objs.host.default_ip()
        } else if let Some(addr) = Worker::resolve_name_to_ip(&name) {
            addr
        } else {
            log::trace!("Unable to find address for name {name:?}");
            // return EFAULT like gethostname
            return Err(Errno::EFAULT);
        };

        log::trace!("Found address {addr} for name {name:?}");

        let addr = u32::from(addr);
        mem.write(addr_ptr, &addr.to_be())?;

        Ok(())
    }
}
