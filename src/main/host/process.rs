use std::cell::RefCell;
use std::num::TryFromIntError;
use std::ops::{Deref, DerefMut};
use std::os::unix::io::{FromRawFd, IntoRawFd};

use nix::unistd::Pid;
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::rootedcell::Root;

use crate::cshadow;
use crate::host::descriptor::{CompatFile, Descriptor};
use crate::host::syscall::format::FmtOptions;
use crate::utility::SyncSendPointer;

use super::descriptor::descriptor_table::DescriptorTable;
use super::memory_manager::MemoryManager;
use super::timer::Timer;

use shadow_shim_helper_rs::HostId;

/// Virtual pid of a shadow process
#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone, Ord, PartialOrd)]
pub struct ProcessId(u32);

impl From<u32> for ProcessId {
    fn from(val: u32) -> Self {
        ProcessId(val)
    }
}

impl TryFrom<libc::pid_t> for ProcessId {
    type Error = TryFromIntError;

    fn try_from(value: libc::pid_t) -> Result<Self, Self::Error> {
        Ok(ProcessId(value.try_into()?))
    }
}

impl From<ProcessId> for u32 {
    fn from(val: ProcessId) -> Self {
        val.0
    }
}

impl TryFrom<ProcessId> for libc::pid_t {
    type Error = TryFromIntError;

    fn try_from(value: ProcessId) -> Result<Self, Self::Error> {
        value.0.try_into()
    }
}

/// Used for C interop.
pub type RustProcess = RootedRefCell<Process>;

pub struct Process {
    cprocess: SyncSendPointer<cshadow::Process>,

    desc_table: RefCell<DescriptorTable>,
}

impl Process {
    /// Takes ownership of `p`.
    ///
    /// # Safety
    ///
    /// `p` must point to a valid c::Process.
    ///
    /// The returned `RootedRefCell<Self>` must not be moved out of its
    /// RootedRc. TODO: statically enforce by wrapping the RootedRefCell in `Pin`,
    /// and making Process `!Unpin`. Probably not worth the work though since
    /// the inner C pointer should be removed soon.
    pub unsafe fn new_from_c(
        root: &Root,
        p: *mut cshadow::Process,
    ) -> RootedRc<RootedRefCell<Self>> {
        // SAFETY: The Process itself is wrapped in a RootedRefCell, which ensures
        // it can only be accessed by one thread at a time. Whenever we access its cprocess,
        // we ensure that the pointer doesn't "escape" in a way that would allow it to be
        // accessed by threads that don't have access to the enclosing Process.
        let cprocess = unsafe { SyncSendPointer::new(p) };
        let desc_table = RefCell::new(DescriptorTable::new());
        let process = RootedRc::new(
            root,
            RootedRefCell::new(
                root,
                Self {
                    cprocess,
                    desc_table,
                },
            ),
        );
        // We're storing a raw pointer to the inner RootedRefCell here. This is a bit
        // fragile, but should be ok since its never moved out of the enclosing RootedRc,
        // so its address won't change. Since the Rust Process owns the C Process, the
        // Rust Process will outlive the C Process.
        //
        // We could store the outer RootedRc, but then need to deal with a reference cycle.
        let process_refcell: &RustProcess = &process;
        unsafe { cshadow::process_setRustProcess(cprocess.ptr(), process_refcell) };
        process
    }

    /// # Safety
    ///
    /// The returned pointer must not outlive the caller's reference to `self`,
    /// or be accessed by threads other than the caller's.
    pub unsafe fn cprocess(&self) -> *mut cshadow::Process {
        self.cprocess.ptr()
    }

    pub fn id(&self) -> ProcessId {
        ProcessId::try_from(unsafe { cshadow::process_getProcessID(self.cprocess.ptr()) }).unwrap()
    }

    pub fn host_id(&self) -> HostId {
        unsafe { cshadow::process_getHostId(self.cprocess.ptr()) }
    }

    fn memory_manager_ptr(&self) -> *mut MemoryManager {
        unsafe { cshadow::process_getMemoryManager(self.cprocess.ptr()) }
    }

    pub fn memory_mut(&mut self) -> &mut MemoryManager {
        unsafe { &mut *self.memory_manager_ptr() }
    }

    pub fn memory(&self) -> &MemoryManager {
        unsafe { &*self.memory_manager_ptr() }
    }

    /// Register a descriptor and return its fd handle.
    pub fn register_descriptor(&self, desc: Descriptor) -> u32 {
        let mut desc_table = self.desc_table.borrow_mut();
        desc_table.add(desc, 0)
    }

    /// Register a descriptor and return its fd handle.
    pub fn register_descriptor_with_min_fd(&self, desc: Descriptor, min_fd: u32) -> u32 {
        let mut desc_table = self.desc_table.borrow_mut();
        desc_table.add(desc, min_fd)
    }

    /// Register a descriptor with a given fd handle and return the descriptor that it replaced.
    pub fn register_descriptor_with_fd(&self, desc: Descriptor, new_fd: u32) -> Option<Descriptor> {
        let mut desc_table = self.desc_table.borrow_mut();
        desc_table.set(new_fd, desc)
    }

    /// Deregister the descriptor with the given fd handle and return it.
    pub fn deregister_descriptor(&self, fd: u32) -> Option<Descriptor> {
        let mut desc_table = self.desc_table.borrow_mut();
        desc_table.remove(fd)
    }

    pub fn strace_logging_options(&self) -> Option<FmtOptions> {
        unsafe { cshadow::process_straceLoggingMode(self.cprocess.ptr()) }.into()
    }

    /// If strace logging is disabled, this function will do nothing and return `None`.
    pub fn with_strace_file<T>(&self, f: impl FnOnce(&mut std::fs::File) -> T) -> Option<T> {
        let fd = unsafe { cshadow::process_getStraceFd(self.cprocess.ptr()) };

        if fd < 0 {
            return None;
        }

        let mut file = unsafe { std::fs::File::from_raw_fd(fd) };
        let rv = f(&mut file);
        file.into_raw_fd();
        Some(rv)
    }

    pub fn descriptor_table(&self) -> impl Deref<Target = DescriptorTable> + '_ {
        self.desc_table.borrow()
    }

    pub fn descriptor_table_mut(&self) -> impl Deref<Target = DescriptorTable> + DerefMut + '_ {
        self.desc_table.borrow_mut()
    }

    pub fn native_pid(&self) -> Pid {
        let pid = unsafe { cshadow::process_getNativePid(self.cprocess.ptr()) };
        Pid::from_raw(pid)
    }

    pub fn realtime_timer(&self) -> &Timer {
        let timer = unsafe { cshadow::process_getRealtimeTimer(self.cprocess.ptr()) };
        unsafe { timer.as_ref().unwrap() }
    }

    pub fn realtime_timer_mut(&mut self) -> &mut Timer {
        let timer = unsafe { cshadow::process_getRealtimeTimer(self.cprocess.ptr()) };
        unsafe { timer.as_mut().unwrap() }
    }
}

impl Drop for Process {
    fn drop(&mut self) {
        unsafe { cshadow::process_free(self.cprocess.ptr()) }
    }
}

mod export {
    use std::ffi::c_int;
    use shadow_shim_helper_rs::notnull::*;

    use crate::core::worker::Worker;
    use crate::host::descriptor::socket::inet::InetSocket;
    use crate::host::descriptor::socket::Socket;
    use crate::host::descriptor::File;

    /// Register a `Descriptor`. This takes ownership of the descriptor and you must not access it
    /// after.
    #[no_mangle]
    pub extern "C" fn process_registerDescriptor(
        proc: *mut cshadow::Process,
        desc: *mut Descriptor,
    ) -> libc::c_int {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        let desc = Descriptor::from_raw(desc).unwrap();

        let fd =
            Worker::with_active_host(|h| proc.borrow(h.root()).register_descriptor(*desc)).unwrap();
        fd.try_into().unwrap()
    }

    /// Get a temporary reference to a descriptor.
    #[no_mangle]
    pub extern "C" fn process_getRegisteredDescriptor(
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *const Descriptor {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };

        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null();
            }
        };

        Worker::with_active_host(
            |h| match proc.borrow(h.root()).descriptor_table().get(handle) {
                Some(d) => d as *const Descriptor,
                None => std::ptr::null(),
            },
        )
        .unwrap()
    }

    /// Get a temporary mutable reference to a descriptor.
    #[no_mangle]
    pub extern "C" fn process_getRegisteredDescriptorMut(
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *mut Descriptor {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };

        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null_mut();
            }
        };

        Worker::with_active_host(|h| {
            match proc.borrow(h.root()).descriptor_table_mut().get_mut(handle) {
                Some(d) => d as *mut Descriptor,
                None => std::ptr::null_mut(),
            }
        })
        .unwrap()
    }

    /// Get a temporary reference to a legacy file.
    #[no_mangle]
    pub unsafe extern "C" fn process_getRegisteredLegacyFile(
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *mut cshadow::LegacyFile {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };

        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null_mut();
            }
        };

        Worker::with_active_host(|h| match proc.borrow(h.root()).descriptor_table().get(handle).map(|x| x.file()) {
            Some(CompatFile::Legacy(file)) => unsafe { file.ptr() },
            Some(CompatFile::New(file)) => {
                // we have a special case for the legacy C TCP objects
                if let File::Socket(Socket::Inet(InetSocket::Tcp(tcp))) = file.inner_file() {
                    tcp.borrow().as_legacy_file()
                } else {
                    log::warn!(
                        "A descriptor exists for fd={}, but it is not a legacy file. Returning NULL.",
                        handle
                    );
                    std::ptr::null_mut()
                }
            }
            None => std::ptr::null_mut(),
        }).unwrap()
    }

    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableShutdownHelper(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            proc.borrow(h.root())
                .descriptor_table_mut()
                .shutdown_helper();
        })
        .unwrap();
    }

    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableRemoveAndCloseAll(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            proc.borrow(h.root())
                .descriptor_table_mut()
                .remove_and_close_all(h);
        })
        .unwrap();
    }

    /// Temporary; meant to be called from process.c.
    ///
    /// Store the given descriptor at the given index. Any previous descriptor that was
    /// stored there will be returned. This consumes a ref to the given descriptor as in
    /// add(), and any returned descriptor must be freed manually.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableSet(
        proc: *const RustProcess,
        index: c_int,
        desc: *mut Descriptor,
    ) -> *mut Descriptor {
        let proc = unsafe { proc.as_ref().unwrap() };
        let descriptor = Descriptor::from_raw(desc);

        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut table = proc.descriptor_table_mut();
            match table.set(index.try_into().unwrap(), *descriptor.unwrap()) {
                Some(d) => Descriptor::into_raw(Box::new(d)),
                None => std::ptr::null_mut(),
            }
        })
        .unwrap()
    }
}
