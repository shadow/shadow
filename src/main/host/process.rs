use std::os::unix::io::{FromRawFd, IntoRawFd};

use nix::unistd::Pid;

use crate::cshadow;
use crate::host::descriptor::CompatDescriptor;
use crate::host::syscall::format::{FmtOptions, StraceFmtMode};

use super::{host::HostId, memory_manager::MemoryManager};

#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone)]
pub struct ProcessId(u32);

impl From<u32> for ProcessId {
    fn from(val: u32) -> Self {
        ProcessId(val)
    }
}

impl From<ProcessId> for u32 {
    fn from(val: ProcessId) -> Self {
        val.0
    }
}

pub struct Process {
    // Placeholder. We don't actually use this yet.
    cprocess: *mut cshadow::Process,
}

impl Process {
    /// For now, this should only be called via Worker to borrow the current
    /// Process, or from the exported functions below. This will ensure there is
    /// only one reference to a given Process in Rust.
    ///
    /// SAFETY: `p` must point to a valid c::Process, to which this Process will
    /// have exclusive access over its lifetime. `p` must outlive the returned object.
    pub unsafe fn borrow_from_c(p: *mut cshadow::Process) -> Self {
        assert!(!p.is_null());
        Process { cprocess: p }
    }

    pub fn cprocess(&self) -> *mut cshadow::Process {
        self.cprocess
    }

    pub fn id(&self) -> ProcessId {
        ProcessId(unsafe { cshadow::process_getProcessID(self.cprocess) })
    }

    pub fn host_id(&self) -> HostId {
        HostId::from(unsafe { cshadow::process_getHostId(self.cprocess) })
    }

    fn memory_manager_ptr(&self) -> *mut MemoryManager {
        unsafe { cshadow::process_getMemoryManager(self.cprocess) }
    }

    pub fn memory_mut(&mut self) -> &mut MemoryManager {
        unsafe { &mut *self.memory_manager_ptr() }
    }

    pub fn memory(&self) -> &MemoryManager {
        unsafe { &*self.memory_manager_ptr() }
    }

    /// Register a descriptor and return its fd handle.
    pub fn register_descriptor(&mut self, desc: CompatDescriptor) -> u32 {
        let desc_table =
            unsafe { cshadow::process_getDescriptorTable(self.cprocess).as_mut() }.unwrap();
        desc_table.add(desc, 0)
    }

    /// Register a descriptor and return its fd handle.
    pub fn register_descriptor_with_min_fd(&mut self, desc: CompatDescriptor, min_fd: u32) -> u32 {
        let desc_table =
            unsafe { cshadow::process_getDescriptorTable(self.cprocess).as_mut() }.unwrap();
        desc_table.add(desc, min_fd)
    }

    /// Register a descriptor with a given fd handle and return the descriptor that it replaced.
    pub fn register_descriptor_with_fd(
        &mut self,
        desc: CompatDescriptor,
        new_fd: u32,
    ) -> Option<CompatDescriptor> {
        let desc_table =
            unsafe { cshadow::process_getDescriptorTable(self.cprocess).as_mut() }.unwrap();
        let replaced_desc = desc_table.set(new_fd, desc);

        if let Some(CompatDescriptor::Legacy(ref replaced_desc)) = replaced_desc {
            unsafe {
                cshadow::descriptor_setOwnerProcess(replaced_desc.ptr(), std::ptr::null_mut())
            };
        }

        replaced_desc
    }

    /// Deregister the descriptor with the given fd handle and return it.
    pub fn deregister_descriptor(&mut self, fd: u32) -> Option<CompatDescriptor> {
        let desc_table =
            unsafe { cshadow::process_getDescriptorTable(self.cprocess).as_mut() }.unwrap();
        let removed_desc = desc_table.remove(fd);

        if let Some(CompatDescriptor::Legacy(ref removed_desc)) = removed_desc {
            unsafe {
                cshadow::descriptor_setOwnerProcess(removed_desc.ptr(), std::ptr::null_mut())
            };
        }

        removed_desc
    }

    pub fn strace_logging_options(&self) -> Option<FmtOptions> {
        StraceFmtMode::try_from(unsafe { cshadow::process_straceLoggingMode(self.cprocess) })
            .unwrap()
            .into()
    }

    /// If strace logging is disabled, this function will do nothing and return `None`.
    pub fn with_strace_file<T>(&self, f: impl FnOnce(&mut std::fs::File) -> T) -> Option<T> {
        let fd = unsafe { cshadow::process_getStraceFd(self.cprocess) };

        if fd < 0 {
            return None;
        }

        let mut file = unsafe { std::fs::File::from_raw_fd(fd) };
        let rv = f(&mut file);
        file.into_raw_fd();
        Some(rv)
    }

    /// Get a reference to the descriptor with the given fd handle.
    pub fn get_descriptor(&self, fd: u32) -> Option<&CompatDescriptor> {
        let desc_table =
            unsafe { cshadow::process_getDescriptorTable(self.cprocess).as_mut() }.unwrap();
        desc_table.get(fd)
    }

    /// Get a mutable reference to the descriptor with the given fd handle.
    pub fn get_descriptor_mut(&mut self, fd: u32) -> Option<&mut CompatDescriptor> {
        let desc_table =
            unsafe { cshadow::process_getDescriptorTable(self.cprocess).as_mut() }.unwrap();
        desc_table.get_mut(fd)
    }

    pub fn native_pid(&self) -> Pid {
        let pid = unsafe { cshadow::process_getNativePid(self.cprocess) };
        Pid::from_raw(pid)
    }

    pub fn raw_mut(&mut self) -> *mut cshadow::Process {
        self.cprocess
    }
}

mod export {
    use super::*;
    use crate::host::descriptor::OwnedLegacyDescriptor;

    /// Register a `CompatDescriptor`. This takes ownership of the descriptor and you must not
    /// access it after.
    #[no_mangle]
    pub extern "C" fn process_registerCompatDescriptor(
        proc: *mut cshadow::Process,
        desc: *mut CompatDescriptor,
    ) -> libc::c_int {
        let mut proc = unsafe { Process::borrow_from_c(proc) };
        let desc = CompatDescriptor::from_raw(desc).unwrap();

        let fd = proc.register_descriptor(*desc);
        fd.try_into().unwrap()
    }

    /// Deregistering a `CompatDescriptor` returns an owned reference to that `CompatDescriptor`,
    /// and you must drop it manually when finished.
    #[no_mangle]
    pub extern "C" fn process_deregisterCompatDescriptor(
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *mut CompatDescriptor {
        let mut proc = unsafe { Process::borrow_from_c(proc) };

        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!(
                    "Attempted to deregister a descriptor with handle {}",
                    handle
                );
                return std::ptr::null_mut();
            }
        };

        match proc.deregister_descriptor(handle) {
            Some(d) => CompatDescriptor::into_raw(Box::new(d)),
            None => std::ptr::null_mut(),
        }
    }

    /// Get a temporary reference to a descriptor.
    #[no_mangle]
    pub extern "C" fn process_getRegisteredCompatDescriptor(
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *const CompatDescriptor {
        let proc = unsafe { Process::borrow_from_c(proc) };

        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null();
            }
        };

        match proc.get_descriptor(handle) {
            Some(d) => d as *const CompatDescriptor,
            None => std::ptr::null(),
        }
    }

    /// Register a `LegacyDescriptor`. This takes ownership of the descriptor and you must
    /// increment the ref count if you are to hold a reference to this descriptor.
    #[no_mangle]
    pub extern "C" fn process_registerLegacyDescriptor(
        proc: *mut cshadow::Process,
        desc: *mut cshadow::LegacyDescriptor,
    ) -> libc::c_int {
        let mut proc = unsafe { Process::borrow_from_c(proc) };
        assert!(!desc.is_null());

        unsafe { cshadow::descriptor_setOwnerProcess(desc, proc.cprocess) };
        let desc = CompatDescriptor::Legacy(OwnedLegacyDescriptor::new(desc));

        let fd = proc.register_descriptor(desc);
        fd.try_into().unwrap()
    }

    /// Unlike the deregister method for the `CompatDescriptor`, you do not need to manually
    /// unref the LegacyDescriptor as it's done automatically.
    #[no_mangle]
    pub extern "C" fn process_deregisterLegacyDescriptor(
        proc: *mut cshadow::Process,
        desc: *mut cshadow::LegacyDescriptor,
    ) {
        let mut proc = unsafe { Process::borrow_from_c(proc) };

        if desc.is_null() {
            return;
        }

        let handle = unsafe { cshadow::descriptor_getHandle(desc) };
        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::warn!(
                    "Attempted to deregister a descriptor with handle {}",
                    handle
                );
                return;
            }
        };

        match proc.deregister_descriptor(handle.try_into().unwrap()) {
            Some(CompatDescriptor::Legacy(removed_desc)) => {
                if removed_desc.ptr() != desc {
                    panic!("Deregistered the wrong descriptor with handle {}", handle);
                }
            }
            Some(_) => {
                panic!(
                    "We were given a pointer to a 'LegacyDescriptor', but when we removed it, \
                     we didn't get a legacy descriptor back"
                );
            }
            None => {
                log::error!("Could not deregister a descriptor with handle {}", handle);
            }
        }
    }

    /// Get a temporary reference to a legacy descriptor.
    #[no_mangle]
    pub extern "C" fn process_getRegisteredLegacyDescriptor(
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *mut cshadow::LegacyDescriptor {
        let proc = unsafe { Process::borrow_from_c(proc) };

        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null_mut();
            }
        };

        match proc.get_descriptor(handle) {
            Some(CompatDescriptor::Legacy(desc)) => desc.ptr(),
            Some(_) => {
                log::warn!("A descriptor exists for fd={}, but it is not a legacy descriptor. Returning NULL.",
                           handle);
                std::ptr::null_mut()
            }
            None => std::ptr::null_mut(),
        }
    }
}
