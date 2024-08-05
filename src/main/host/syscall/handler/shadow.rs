use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::ForeignArrayPtr;
use crate::utility::case_insensitive_eq;

impl SyscallHandler {
    log_syscall!(shadow_yield, /* rv */ std::ffi::c_int);
    pub fn shadow_yield(_ctx: &mut SyscallContext) -> Result<(), Errno> {
        Ok(())
    }

    log_syscall!(shadow_init_memory_manager, /* rv */ std::ffi::c_int);
    pub fn shadow_init_memory_manager(ctx: &mut SyscallContext) -> Result<(), Errno> {
        if !ctx.objs.host.params.use_mem_mapper {
            log::trace!("Not initializing memory mapper");
            return Ok(());
        }

        log::trace!("Initializing memory mapper");

        let mut memory_manager = ctx.objs.process.memory_borrow_mut();
        if !memory_manager.has_mapper() {
            memory_manager.init_mapper(ctx.objs)
        }

        Ok(())
    }

    log_syscall!(
        shadow_hostname_to_addr_ipv4,
        /* rv */ std::ffi::c_int,
        /* name_ptr */ *const std::ffi::c_char,
        /* name_len */ u64,
        /* addr_ptr */ *const std::ffi::c_void,
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

        // TODO: Don't add 1 byte to length (if the application gave us a length of X bytes, don't
        // read more than X bytes). This might not be valid memory. I'm guessing we do this now to
        // avoid needing to allocate a new `CString` with a NUL byte when calling into the C DNS
        // code. But in the future when the DNS code is in rust, we won't need NUL-terminated
        // strings.

        let name_ptr = name_ptr.cast::<u8>();
        // add one byte to the length and hope that it contains a NUL
        let name_ptr = ForeignArrayPtr::new(name_ptr, name_len + 1);
        let addr_ptr = addr_ptr.cast::<u32>();

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let lookup_name_ref = mem.memory_ref_prefix(name_ptr)?;
        let lookup_name = lookup_name_ref.get_cstr()?;
        let lookup_name_bytes = lookup_name.to_bytes();

        if case_insensitive_eq(lookup_name_bytes, &b"localhost"[..]) {
            let addr = u32::from(std::net::Ipv4Addr::LOCALHOST);
            mem.write(addr_ptr, &addr.to_be())?;
            log::trace!("Returning loopback address for localhost");
            return Ok(());
        }

        // TODO: why do we truncate at `NI_MAXHOST`?
        let max_len = libc::NI_MAXHOST.try_into().unwrap();
        let host_name = ctx.objs.host.info().name.as_bytes();
        let host_name = &host_name[..std::cmp::min(host_name.len(), max_len)];
        let lookup_name_bytes =
            &lookup_name_bytes[..std::cmp::min(lookup_name_bytes.len(), max_len)];

        let addr = if case_insensitive_eq(lookup_name_bytes, host_name) {
            log::trace!("Using default address for my own hostname {lookup_name:?}");
            Some(ctx.objs.host.default_ip())
        } else {
            log::trace!("Looking up name {lookup_name:?}");
            Worker::resolve_name_to_ip(lookup_name)
        };

        let Some(addr) = addr else {
            log::trace!("Unable to find address for name {lookup_name:?}");
            // return EFAULT like gethostname
            return Err(Errno::EFAULT);
        };

        log::trace!("Found address {addr} for name {lookup_name:?}");

        let addr = u32::from(addr);
        mem.write(addr_ptr, &addr.to_be())?;

        Ok(())
    }
}
