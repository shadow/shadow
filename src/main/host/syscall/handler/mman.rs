use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;

use linux_api::errno::Errno;
use linux_api::fcntl::OFlag;
use linux_api::mman::{MapFlags, ProtFlags};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::{CompatFile, FileState};
use crate::host::memory_manager::AllocdMem;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler, ThreadContext};
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    log_syscall!(
        brk,
        /* rv */ std::ffi::c_int,
        /* addr */ *const std::ffi::c_void,
    );
    pub fn brk(
        ctx: &mut SyscallContext,
        addr: ForeignPtr<u8>,
    ) -> Result<ForeignPtr<u8>, SyscallError> {
        // delegate to the memory manager
        let mut memory_manager = ctx.objs.process.memory_borrow_mut();
        memory_manager.handle_brk(ctx.objs, addr)
    }

    // <https://github.com/torvalds/linux/tree/v6.3/mm/mremap.c#L895>
    // ```
    // SYSCALL_DEFINE5(mremap, unsigned long, addr, unsigned long, old_len,
    //                 unsigned long, new_len, unsigned long, flags,
    //                 unsigned long, new_addr)
    // ```
    log_syscall!(
        mremap,
        /* rv */ *const std::ffi::c_void,
        /* old_address */ *const std::ffi::c_void,
        /* old_size */ std::ffi::c_ulong,
        /* new_size */ std::ffi::c_ulong,
        /* flags */ linux_api::mman::MRemapFlags,
        /* new_address */ *const std::ffi::c_void,
    );
    pub fn mremap(
        ctx: &mut SyscallContext,
        old_addr: std::ffi::c_ulong,
        old_size: std::ffi::c_ulong,
        new_size: std::ffi::c_ulong,
        flags: std::ffi::c_ulong,
        new_addr: std::ffi::c_ulong,
    ) -> Result<ForeignPtr<u8>, SyscallError> {
        let old_addr: usize = old_addr.try_into().unwrap();
        let old_size: usize = old_size.try_into().unwrap();
        let new_size: usize = new_size.try_into().unwrap();
        let new_addr: usize = new_addr.try_into().unwrap();

        // check for truncated flag bits (use u32 instead of i32 to prevent sign extension when
        // casting from signed to unsigned)
        if flags as u32 as u64 != flags {
            warn_once_then_trace!("Ignoring truncated flags from mremap: {flags}");
        }

        let flags = flags as i32;

        let old_addr = ForeignPtr::<()>::from(old_addr).cast::<u8>();
        let new_addr = ForeignPtr::<()>::from(new_addr).cast::<u8>();

        // delegate to the memory manager
        let mut memory_manager = ctx.objs.process.memory_borrow_mut();
        memory_manager.handle_mremap(ctx.objs, old_addr, old_size, new_size, flags, new_addr)
    }

    // <https://github.com/torvalds/linux/tree/v6.3/mm/mmap.c#L2786>
    // ```
    // SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
    // ```
    log_syscall!(
        munmap,
        /* rv */ std::ffi::c_int,
        /* addr */ *const std::ffi::c_void,
        /* length */ usize,
    );
    pub fn munmap(
        ctx: &mut SyscallContext,
        addr: std::ffi::c_ulong,
        len: usize,
    ) -> Result<(), SyscallError> {
        let addr: usize = addr.try_into().unwrap();
        let addr = ForeignPtr::<()>::from(addr).cast::<u8>();

        // delegate to the memory manager
        let mut memory_manager = ctx.objs.process.memory_borrow_mut();
        memory_manager.handle_munmap(ctx.objs, addr, len)
    }

    // <https://github.com/torvalds/linux/tree/v6.3/mm/mprotect.c#L849>
    // ```
    // SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len, unsigned long, prot)
    // ```
    log_syscall!(
        mprotect,
        /* rv */ std::ffi::c_int,
        /* addr */ *const std::ffi::c_void,
        /* len */ usize,
        /* prot */ linux_api::mman::ProtFlags,
    );
    pub fn mprotect(
        ctx: &mut SyscallContext,
        addr: std::ffi::c_ulong,
        len: usize,
        prot: std::ffi::c_ulong,
    ) -> Result<(), SyscallError> {
        let addr: usize = addr.try_into().unwrap();
        let addr = ForeignPtr::<()>::from(addr).cast::<u8>();

        let Some(prot) = ProtFlags::from_bits(prot) else {
            let unrecognized = ProtFlags::from_bits_retain(prot).difference(ProtFlags::all());
            log_once_per_value_at_level!(
                unrecognized,
                ProtFlags,
                log::Level::Warn,
                log::Level::Debug,
                "Unrecognized prot flag: {:#x}",
                unrecognized.bits()
            );
            return Err(Errno::EINVAL.into());
        };

        // delegate to the memory manager
        let mut memory_manager = ctx.objs.process.memory_borrow_mut();
        memory_manager.handle_mprotect(ctx.objs, addr, len, prot)
    }

    // <https://github.com/torvalds/linux/tree/v6.3/arch/x86/kernel/sys_x86_64.c#L86>
    // ```
    // SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
    //                 unsigned long, prot, unsigned long, flags,
    //                 unsigned long, fd, unsigned long, off)
    // ```
    log_syscall!(
        mmap,
        /* rv */ *const std::ffi::c_void,
        /* addr */ *const std::ffi::c_void,
        /* length */ usize,
        /* prot */ linux_api::mman::ProtFlags,
        /* flags */ linux_api::mman::MapFlags,
        /* fd */ std::ffi::c_ulong,
        /* offset */ std::ffi::c_ulong,
    );
    pub fn mmap(
        ctx: &mut SyscallContext,
        addr: std::ffi::c_ulong,
        len: std::ffi::c_ulong,
        prot: std::ffi::c_ulong,
        flags: std::ffi::c_ulong,
        fd: std::ffi::c_ulong,
        offset: std::ffi::c_ulong,
    ) -> Result<ForeignPtr<u8>, Errno> {
        log::trace!("mmap called on fd {fd} for {len} bytes");

        let addr: usize = addr.try_into().unwrap();
        let addr = ForeignPtr::<()>::from(addr).cast::<u8>();

        let len: usize = len.try_into().unwrap();

        let offset = offset as i64;

        let Some(prot) = ProtFlags::from_bits(prot) else {
            let unrecognized = ProtFlags::from_bits_retain(prot).difference(ProtFlags::all());
            log_once_per_value_at_level!(
                unrecognized,
                ProtFlags,
                log::Level::Warn,
                log::Level::Debug,
                "Unrecognized prot flag: {:#x}",
                unrecognized.bits()
            );
            return Err(Errno::EINVAL);
        };
        let Some(flags) = MapFlags::from_bits(flags) else {
            let unrecognized = MapFlags::from_bits_retain(flags).difference(MapFlags::all());
            log_once_per_value_at_level!(
                unrecognized,
                MapFlags,
                log::Level::Warn,
                log::Level::Debug,
                "Unrecognized map flag: {:#x}",
                unrecognized.bits()
            );
            return Err(Errno::EINVAL);
        };

        // at least one of these values is required according to man page
        let required_flags =
            MapFlags::MAP_PRIVATE | MapFlags::MAP_SHARED | MapFlags::MAP_SHARED_VALIDATE;

        // need non-zero len, and at least one of the above options
        if len == 0 || !required_flags.intersects(flags) {
            log::debug!("Invalid len ({len}), prot ({prot:?}), or flags ({flags:?})");
            return Err(Errno::EINVAL);
        }

        // we ignore the fd on anonymous mappings, otherwise it must refer to a regular file
        // TODO: why does this fd <= 2 exist?
        if fd <= 2 && !flags.contains(MapFlags::MAP_ANONYMOUS) {
            log::debug!("Invalid fd {fd} and MAP_ANONYMOUS is not set in flags {flags:?}");
            return Err(Errno::EBADF);
        }

        // we only need a file if it's not an anonymous mapping
        let file = if flags.contains(MapFlags::MAP_ANONYMOUS) {
            None
        } else {
            let file = {
                // get the descriptor, or return early if it doesn't exist
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                let desc = Self::get_descriptor(&desc_table, fd)?;

                let CompatFile::Legacy(file) = desc.file() else {
                    // this syscall uses a regular file, which is implemented in C
                    return Err(Errno::EINVAL);
                };

                file.ptr()
            };

            assert!(!file.is_null());

            if unsafe { c::legacyfile_getStatus(file) }.contains(FileState::CLOSED) {
                // A file that is referenced in the descriptor table should never be a closed file.
                // File handles (fds) are handles to open files, so if we have a file handle to a
                // closed file, then there's an error somewhere in Shadow. Shadow's TCP sockets do
                // close themselves even if there are still file handles (see
                // `_tcp_endOfFileSignalled`), so we can't make this a panic.
                log::warn!("File {file:p} (fd={fd}) is closed");
                return Err(Errno::EBADF);
            }

            if unsafe { c::legacyfile_getType(file) } != c::_LegacyFileType_DT_FILE {
                log::debug!("Descriptor exists for fd {fd}, but is not a regular file type");
                return Err(Errno::EACCES);
            }

            // success; we know we have a file type descriptor
            Some(file as *mut c::RegularFile)
        };

        // this fd exists in the plugin and not shadow; make sure to close this before returning (no
        // RAII)
        let plugin_fd = file.map(|file| Self::open_plugin_file(ctx.objs, fd, file));

        // the file is None for an anonymous mapping, or a non-null Some otherwise
        let Ok(plugin_fd) = plugin_fd.transpose() else {
            log::warn!("mmap on fd {fd} for {len} bytes failed");
            return Err(Errno::EACCES);
        };

        // delegate execution of the mmap itself to the memory manager
        let mut memory_manager = ctx.objs.process.memory_borrow_mut();
        let mmap_result = memory_manager.do_mmap(
            ctx.objs,
            addr,
            len,
            prot,
            flags,
            plugin_fd.unwrap_or(-1),
            offset,
        );

        log::trace!(
            "Plugin-native mmap syscall at plugin addr {addr:p} with plugin fd {fd} for \
            {len} bytes returned {mmap_result:?}"
        );

        // close the file we asked them to open
        if let Some(plugin_fd) = plugin_fd {
            Self::close_plugin_file(ctx.objs, plugin_fd);
        }

        mmap_result
    }

    fn open_plugin_file(
        ctx: &ThreadContext,
        fd: std::ffi::c_ulong,
        file: *mut c::RegularFile,
    ) -> Result<i32, ()> {
        assert!(!file.is_null());

        log::trace!("Trying to open file {fd} in the plugin");

        // Make sure we don't open special files like `/dev/urandom` in the plugin via mmap. We
        // allow `/etc/localtime`, which should have been swapped with `/usr/share/zoneinfo/Etc/UTC`
        // in `regularfile_openat`.
        let file_type = unsafe { c::regularfile_getType(file) };
        if file_type != c::_FileType_FILE_TYPE_REGULAR
            && file_type != c::_FileType_FILE_TYPE_LOCALTIME
        {
            warn_once_then_debug!("Tried to mmap a non-regular non-localtime file");
            return Err(());
        }

        let native_fd = unsafe { c::regularfile_getOSBackedFD(file) };

        // the file is in the shadow process, and we want to open it in the plugin
        let Some(path) = Self::create_persistent_mmap_path(native_fd) else {
            log::trace!("RegularFile {fd} has a NULL path");
            return Err(());
        };

        let path_bytes = path.as_os_str().as_bytes();

        // TODO: do we really want to continue if we need to truncate the path and we already know
        // the truncated path will be incorrect?

        // we need enough mem for the string, but no more than PATH_MAX (with space for a NUL)
        let path_len = std::cmp::min(path_bytes.len(), libc::PATH_MAX as usize - 1);
        assert!(path_len > 0);

        let path_bytes = &path_bytes[..path_len];

        log::trace!("Opening path '{}' in plugin", path.display());

        // get some memory in the plugin to write the path of the file to open (an extra 1 for NUL);
        // must free this, but will panic if borrowing the memory manager
        let plugin_buffer = AllocdMem::<u8>::new(ctx, path_len + 1);

        {
            let mut mem = ctx.process.memory_borrow_mut();

            // write the path to the plugin
            if let Err(e) = mem.copy_to_ptr(plugin_buffer.ptr().slice(..path_len), path_bytes) {
                log::warn!("Unable to write string to allocated buffer: {e}");
                std::mem::drop(mem);
                plugin_buffer.free(ctx);
                return Err(());
            }

            // write the NUL to the plugin
            if let Err(e) = mem.copy_to_ptr(plugin_buffer.ptr().slice(path_len..), &[0]) {
                log::warn!("Unable to write NUL to allocated buffer: {e}");
                std::mem::drop(mem);
                plugin_buffer.free(ctx);
                return Err(());
            }
        }

        // attempt to open the file in the plugin with the same flags as what the shadow RegularFile
        // object has

        // from man 2 open
        let creation_flags = OFlag::empty()
            | OFlag::O_CLOEXEC
            | OFlag::O_CREAT
            | OFlag::O_DIRECTORY
            | OFlag::O_EXCL
            | OFlag::O_NOCTTY
            | OFlag::O_NOFOLLOW
            | OFlag::O_TMPFILE
            | OFlag::O_TRUNC;

        // the flags linux is using
        let native_flags = OFlag::from_bits_retain(unsafe {
            libc::fcntl(c::regularfile_getOSBackedFD(file), libc::F_GETFL)
        });

        // get original flags that were used to open the file
        let mut flags = OFlag::from_bits_retain(unsafe { c::regularfile_getFlagsAtOpen(file) });
        // use only the file creation flags, except O_CLOEXEC
        flags &= creation_flags.difference(OFlag::O_CLOEXEC);
        // add any file access mode and file status flags that shadow doesn't implement
        flags |= native_flags.difference(OFlag::from_bits_retain(unsafe { c::SHADOW_FLAG_MASK }));
        // add any flags that shadow implements
        flags |= OFlag::from_bits_retain(unsafe { c::regularfile_getShadowFlags(file) });
        // be careful not to try re-creating or truncating it
        flags -= OFlag::O_CREAT | OFlag::O_EXCL | OFlag::O_TMPFILE | OFlag::O_TRUNC;
        // don't use O_NOFOLLOW since it will prevent the plugin from opening the
        // /proc/<shadow-pid>/fd/<linux-fd> file, which is a symbolic link
        flags -= OFlag::O_NOFOLLOW;

        let mode = unsafe { c::regularfile_getModeAtOpen(file) };

        // instruct the plugin to open the file at the path we sent
        let (process_ctx, thread) = ctx.split_thread();
        let open_result = thread.native_open(
            &process_ctx,
            plugin_buffer.ptr().ptr(),
            flags.bits() as i32,
            mode as i32,
        );

        plugin_buffer.free(ctx);

        let open_result = match open_result {
            Ok(x) => x,
            Err(e) => {
                log::trace!(
                    "Failed to open path '{}' in plugin, error {e}",
                    path.display()
                );
                return Err(());
            }
        };

        log::trace!(
            "Successfully opened path '{}' in plugin, got plugin fd {open_result}",
            path.display(),
        );

        Ok(open_result)
    }

    /// Instruct the plugin to close the file at the given fd.
    fn close_plugin_file(ctx: &ThreadContext, plugin_fd: i32) {
        let (ctx, thread) = ctx.split_thread();
        let result = thread.native_close(&ctx, plugin_fd);

        if let Err(e) = result {
            log::trace!("Failed to close file at fd {plugin_fd} in plugin, error {e}");
        } else {
            log::trace!("Successfully closed file at fd {plugin_fd} in plugin");
        }
    }

    /// Get a path to a persistent file that can be mmapped in a child process, where any I/O
    /// operations on the map will be linked to the original file. Returns a path, or `None` if we
    /// are unable to create an accessible path.
    fn create_persistent_mmap_path(native_fd: std::ffi::c_int) -> Option<PathBuf> {
        assert!(native_fd >= 0);

        // Return a path that is linked to the I/O operations of the file. Our current strategy is
        // to have the plugin open and map the /proc/<shadow-pid>/fd/<linux-fd> file, which
        // guarantees that the I/O on the Shadow file object and the new map will be linked to the
        // linux file. TODO: using procfs in this was may or may not work if trying to mmap a
        // device.
        //
        // NOTE: If we need to change this implementation, there are two tricky cases that need to
        // be considered: files opened with O_TMPFILE (with a directory pathname), and files that
        // were opened and then immediately unlinked (so only the anonymous fd remains). The procfs
        // solution above handles both of these issues.

        let pid_string = std::process::id().to_string();
        let native_fd_string = native_fd.to_string();

        // We do not use the original file path here, because that path could have been re-linked to
        // a different file since this file was opened.
        let path: PathBuf = ["/proc", &pid_string, "fd", &native_fd_string]
            .iter()
            .collect();

        // make sure the path is accessible
        if !path.exists() {
            log::warn!(
                "Unable to produce a persistent mmap path for file (linux file {native_fd})"
            );
            return None;
        }

        log::trace!(
            "RegularFile (linux file {native_fd}) is persistent in procfs at {}",
            path.display()
        );

        Some(path)
    }
}
