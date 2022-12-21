use std::cell::{Ref, RefCell, RefMut};
use std::ffi::{CStr, CString};
use std::num::TryFromIntError;
use std::ops::{Deref, DerefMut};
use std::os::unix::io::{FromRawFd, IntoRawFd};
use std::path::PathBuf;

use log::{debug, info, trace};
use nix::fcntl::OFlag;
use nix::sys::stat::Mode;
use nix::unistd::Pid;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::shim_shmem::ProcessShmem;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shmem::allocator::ShMemBlock;

use crate::core::work::task::TaskRef;
use crate::cshadow;
use crate::host::descriptor::{CompatFile, Descriptor};
use crate::host::syscall::formatter::FmtOptions;
use crate::utility::{pathbuf_to_nul_term_cstring, SyncSendPointer};

use super::descriptor::descriptor_table::DescriptorTable;
use super::host::Host;
use super::memory_manager::MemoryManager;
use super::syscall::formatter::StraceFmtMode;
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
    id: ProcessId,
    host_id: HostId,

    // unique id of the program that this process should run
    name: CString,

    // the name and path to the executable that we will exec
    plugin_name: CString,
    plugin_path: CString,

    // absolute path to the process's working directory
    working_dir: CString,

    // environment variables to pass to exec
    envv: Vec<CString>,

    // argument strings to pass to exec
    argv: Vec<CString>,

    // Shared memory allocation for shared state with shim.
    shim_shared_mem_block: ShMemBlock<'static, ProcessShmem>,

    // process boot and shutdown variables
    start_time: EmulatedTime,
    stop_time: Option<EmulatedTime>,

    strace_logging_mode: StraceFmtMode,

    desc_table: RefCell<DescriptorTable>,
    memory_manager: RefCell<Option<MemoryManager>>,
    itimer_real: RefCell<Timer>,
}

fn itimer_real_expiration(host: &Host, pid: ProcessId) {
    let Some(process) = host.process_borrow(pid) else {
        debug!("Process {:?} no longer exists", pid);
        return;
    };
    let process = process.borrow(host.root());
    let timer = process.itimer_real.borrow();
    let mut siginfo: cshadow::siginfo_t = unsafe { std::mem::zeroed() };
    // The siginfo_t structure only has an i32. Presumably we want to just truncate in
    // case of overflow.
    let expiration_count = timer.expiration_count() as i32;
    unsafe { cshadow::process_initSiginfoForAlarm(&mut siginfo, expiration_count) };
    unsafe { cshadow::process_signal(process.cprocess(), std::ptr::null_mut(), &siginfo) };
}

impl Process {
    /// # Safety
    ///
    /// The returned `RootedRefCell<Self>` must not be moved out of its
    /// RootedRc. TODO: statically enforce by wrapping the RootedRefCell in
    /// `Pin`, and making Process `!Unpin`. Probably not worth the work though
    /// since the inner C pointer should be removed soon, and then we can remove
    /// this requirement.
    pub unsafe fn new(
        host: &Host,
        process_id: ProcessId,
        start_time: SimulationTime,
        stop_time: Option<SimulationTime>,
        plugin_name: &CStr,
        plugin_path: &CStr,
        mut envv: Vec<CString>,
        argv: Vec<CString>,
        pause_for_debugging: bool,
        use_legacy_working_dir: bool,
        use_shim_syscall_handler: bool,
        strace_logging_mode: StraceFmtMode,
    ) -> RootedRc<RootedRefCell<Self>> {
        debug_assert!(stop_time.is_none() || stop_time.unwrap() > start_time);

        let desc_table = RefCell::new(DescriptorTable::new());
        let memory_manager = RefCell::new(None);
        let itimer_real = RefCell::new(Timer::new(move |host| {
            itimer_real_expiration(host, process_id)
        }));
        let plugin_name = plugin_name.to_owned();
        let plugin_path = plugin_path.to_owned();
        let name = CString::new(format!(
            "{host_name}.{exe_name}.{id}",
            host_name = host.name(),
            exe_name = plugin_name.to_str().unwrap(),
            id = u32::from(process_id)
        ))
        .unwrap();

        let shim_shared_mem =
            ProcessShmem::new(&host.shim_shmem_lock_borrow().unwrap().root, host.id());
        let shim_shared_mem_block =
            shadow_shmem::allocator::Allocator::global().alloc(shim_shared_mem);

        let working_dir = pathbuf_to_nul_term_cstring(if use_legacy_working_dir {
            nix::unistd::getcwd().unwrap()
        } else {
            std::fs::canonicalize(host.data_dir_path()).unwrap()
        });

        // TODO: ensure no duplicate env vars.
        envv.push(
            CString::new(format!(
                "SHADOW_SHM_PROCESS_BLK={}",
                shim_shared_mem_block.serialize().encode_to_string()
            ))
            .unwrap(),
        );
        envv.push(
            CString::new(format!(
                "SHADOW_LOG_FILE={}",
                Self::static_output_file_name(name.to_str().unwrap(), host, "shimlog")
                    .to_str()
                    .unwrap()
            ))
            .unwrap(),
        );
        if !use_shim_syscall_handler {
            envv.push(CString::new("SHADOW_DISABLE_SHIM_SYSCALL=TRUE").unwrap());
        }

        // We've initialized all the parts of Self *except* for the cprocess.
        // `process_new` needs the Rust process though, so we create that now with
        // a NULL cprocess, then create the cprocess, then add it to the Self.
        let process = RootedRc::new(
            host.root(),
            RootedRefCell::new(
                host.root(),
                Self {
                    id: process_id,
                    host_id: host.id(),
                    // We set this to non-null below.
                    cprocess: unsafe { SyncSendPointer::new(std::ptr::null_mut()) },
                    argv,
                    envv,
                    working_dir,
                    shim_shared_mem_block,
                    memory_manager,
                    desc_table,
                    itimer_real,
                    start_time: EmulatedTime::SIMULATION_START + start_time,
                    stop_time: stop_time.map(|t| EmulatedTime::SIMULATION_START + t),
                    name,
                    plugin_name,
                    plugin_path,
                    strace_logging_mode,
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
        let cprocess = unsafe {
            cshadow::process_new(
                process_refcell,
                host,
                process_id.try_into().unwrap(),
                pause_for_debugging,
            )
        };
        // SAFETY: The Process itself is wrapped in a RootedRefCell, which ensures
        // it can only be accessed by one thread at a time. Whenever we access its cprocess,
        // we ensure that the pointer doesn't "escape" in a way that would allow it to be
        // accessed by threads that don't have access to the enclosing Process.
        let cprocess = unsafe { SyncSendPointer::new(cprocess) };
        process.borrow_mut(host.root()).cprocess = cprocess;

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
        self.id
    }

    pub fn host_id(&self) -> HostId {
        self.host_id
    }

    pub fn schedule(&self, host: &Host) {
        let id = self.id();
        match self.stop_time {
            Some(t) if self.start_time >= t => {
                info!(
                    "Not scheduling process with start:{:?} after stop:{:?}",
                    self.start_time, t
                );
                return;
            }
            _ => (),
        };
        let task = TaskRef::new(move |host| {
            let process = host.process_borrow(id).unwrap();
            process.borrow(host.root()).start(host);
        });
        host.schedule_task_at_emulated_time(task, self.start_time);

        if let Some(stop_time) = self.stop_time {
            let task = TaskRef::new(move |host| {
                let process = host.process_borrow(id).unwrap();
                let cprocess = unsafe { process.borrow(host.root()).cprocess() };
                unsafe { cshadow::process_stop(cprocess) };
            });
            host.schedule_task_at_emulated_time(task, stop_time);
        }
    }

    fn start(&self, host: &Host) {
        self.open_stdio_file_helper(
            libc::STDIN_FILENO as u32,
            "/dev/null".into(),
            OFlag::O_RDONLY,
        );

        let name = self.output_file_name(host, "stdout");
        self.open_stdio_file_helper(libc::STDOUT_FILENO as u32, name, OFlag::O_WRONLY);

        let name = self.output_file_name(host, "stderr");
        self.open_stdio_file_helper(libc::STDERR_FILENO as u32, name, OFlag::O_WRONLY);

        let argv_ptrs: Vec<*const i8> = self
            .argv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of argv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();

        let envv_ptrs: Vec<*const i8> = self
            .envv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of envv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();
        unsafe { cshadow::process_start(self.cprocess(), argv_ptrs.as_ptr(), envv_ptrs.as_ptr()) };
    }

    fn open_stdio_file_helper(
        &self,
        fd: u32,
        path: PathBuf,
        access_mode: OFlag,
    ) -> *mut cshadow::RegularFile {
        let stdfile = unsafe { cshadow::regularfile_new() };
        let cwd = nix::unistd::getcwd().unwrap();
        let path = pathbuf_to_nul_term_cstring(path);
        let cwd = pathbuf_to_nul_term_cstring(cwd);
        let errorcode = unsafe {
            cshadow::regularfile_open(
                stdfile,
                path.as_ptr(),
                (access_mode | OFlag::O_CREAT | OFlag::O_TRUNC).bits(),
                (Mode::S_IRUSR | Mode::S_IWUSR | Mode::S_IRGRP | Mode::S_IROTH).bits(),
                cwd.as_ptr(),
            )
        };
        if errorcode != 0 {
            panic!(
                "Opening {}: {:?}",
                path.to_str().unwrap(),
                nix::errno::Errno::from_i32(-errorcode)
            );
        }
        let desc = unsafe {
            Descriptor::from_legacy_file(stdfile as *mut cshadow::LegacyFile, OFlag::empty())
        };
        let prev = self.descriptor_table_borrow_mut().set(fd, desc);
        assert!(prev.is_none());
        trace!(
            "Successfully opened fd {} at {}",
            fd,
            path.to_str().unwrap()
        );
        stdfile
    }

    fn output_file_name(&self, host: &Host, basename: &str) -> PathBuf {
        Self::static_output_file_name(self.name(), host, basename)
    }

    // Needed during early init, before `Self` is created.
    fn static_output_file_name(name: &str, host: &Host, basename: &str) -> PathBuf {
        let mut dirname = host.data_dir_path().to_owned();
        let basename = format!("{}.{}", name, basename);
        dirname.push(basename);
        dirname
    }

    pub fn name(&self) -> &str {
        self.name.to_str().unwrap()
    }

    pub fn plugin_name(&self) -> &str {
        self.plugin_name.to_str().unwrap()
    }

    pub fn plugin_path(&self) -> &str {
        self.plugin_path.to_str().unwrap()
    }

    #[track_caller]
    pub fn memory_borrow_mut(&self) -> impl Deref<Target = MemoryManager> + DerefMut + '_ {
        RefMut::map(self.memory_manager.borrow_mut(), |mm| mm.as_mut().unwrap())
    }

    #[track_caller]
    pub fn memory_borrow(&self) -> impl Deref<Target = MemoryManager> + '_ {
        Ref::map(self.memory_manager.borrow(), |mm| mm.as_ref().unwrap())
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

    #[track_caller]
    pub fn descriptor_table_borrow(&self) -> impl Deref<Target = DescriptorTable> + '_ {
        self.desc_table.borrow()
    }

    #[track_caller]
    pub fn descriptor_table_borrow_mut(
        &self,
    ) -> impl Deref<Target = DescriptorTable> + DerefMut + '_ {
        self.desc_table.borrow_mut()
    }

    pub fn native_pid(&self) -> Pid {
        let pid = unsafe { cshadow::process_getNativePid(self.cprocess.ptr()) };
        Pid::from_raw(pid)
    }

    #[track_caller]
    pub fn realtime_timer_borrow(&self) -> impl Deref<Target = Timer> + '_ {
        self.itimer_real.borrow()
    }

    #[track_caller]
    pub fn realtime_timer_borrow_mut(&self) -> impl Deref<Target = Timer> + DerefMut + '_ {
        self.itimer_real.borrow_mut()
    }
}

impl Drop for Process {
    fn drop(&mut self) {
        unsafe { cshadow::process_free(self.cprocess.ptr()) }
    }
}

mod export {
    use std::ffi::{c_char, c_int};
    use std::os::raw::c_void;

    use log::{trace, warn};
    use shadow_shim_helper_rs::notnull::*;
    use shadow_shim_helper_rs::shim_shmem::export::ShimShmemProcess;

    use crate::core::worker::Worker;
    use crate::cshadow::CEmulatedTime;
    use crate::host::descriptor::socket::inet::InetSocket;
    use crate::host::descriptor::socket::Socket;
    use crate::host::descriptor::File;
    use crate::host::memory_manager::{ProcessMemoryRef, ProcessMemoryRefMut};
    use crate::host::syscall_types::{PluginPtr, TypedPluginPtr};
    use crate::host::thread::ThreadRef;
    use crate::utility::callback_queue::CallbackQueue;

    use super::*;

    /// Register a `Descriptor`. This takes ownership of the descriptor and you must not access it
    /// after.
    #[no_mangle]
    pub extern "C" fn process_registerDescriptor(
        proc: *mut cshadow::Process,
        desc: *mut Descriptor,
    ) -> libc::c_int {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        let desc = Descriptor::from_raw(desc).unwrap();

        let fd = Worker::with_active_host(|h| {
            proc.borrow(h.root())
                .descriptor_table_borrow_mut()
                .register_descriptor(*desc)
        })
        .unwrap();
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

        Worker::with_active_host(|h| {
            match proc.borrow(h.root()).descriptor_table_borrow().get(handle) {
                Some(d) => d as *const Descriptor,
                None => std::ptr::null(),
            }
        })
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
            match proc
                .borrow(h.root())
                .descriptor_table_borrow_mut()
                .get_mut(handle)
            {
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

        Worker::with_active_host(|h| match proc.borrow(h.root()).descriptor_table_borrow().get(handle).map(|x| x.file()) {
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

    #[no_mangle]
    pub extern "C" fn _process_readPtr(
        proc: *const RustProcess,
        dst: *mut c_void,
        src: cshadow::PluginPtr,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let src = TypedPluginPtr::new::<u8>(src.into(), n);
        let dst = unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(dst) as *mut u8, n) };

        Worker::with_active_host(|h| {
            match proc
                .borrow(h.root())
                .memory_borrow()
                .copy_from_ptr(dst, src)
            {
                Ok(_) => 0,
                Err(e) => {
                    trace!("Couldn't read {:?} into {:?}: {:?}", src, dst, e);
                    -(e as i32)
                }
            }
        })
        .unwrap()
    }

    /// Write data to this writer's memory.
    #[no_mangle]
    pub unsafe extern "C" fn _process_writePtr(
        proc: *const RustProcess,
        dst: cshadow::PluginPtr,
        src: *const c_void,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let dst = TypedPluginPtr::new::<u8>(dst.into(), n);
        let src = unsafe { std::slice::from_raw_parts(notnull_debug(src) as *const u8, n) };
        Worker::with_active_host(|h| {
            match proc
                .borrow(h.root())
                .memory_borrow_mut()
                .copy_to_ptr(dst, src)
            {
                Ok(_) => 0,
                Err(e) => {
                    trace!("Couldn't write {:?} into {:?}: {:?}", src, dst, e);
                    -(e as i32)
                }
            }
        })
        .unwrap()
    }

    /// Get a read-accessor to the specified plugin memory.
    /// Must be freed via `memorymanager_freeReader`.
    #[no_mangle]
    pub unsafe extern "C" fn _process_getReadablePtr(
        proc: *const RustProcess,
        plugin_src: cshadow::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRef<'static, u8> {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src: PluginPtr = plugin_src.into();
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let memory = proc.memory_borrow();
            let memory_ref = memory.memory_ref(TypedPluginPtr::new::<u8>(plugin_src, n));
            match memory_ref {
                Ok(mr) => {
                    // SAFETY: This should only be called from process.c, which
                    // tries to detect incompatible borrows.
                    let mr = unsafe { std::mem::transmute::<ProcessMemoryRef<'_, u8>, ProcessMemoryRef<'static, u8>>(mr)};
                    Box::into_raw(Box::new(mr))
                },
                Err(e) => {
                    warn!("Failed to get memory ref: {:?}", e);
                    std::ptr::null_mut()
                }
            }
        }).unwrap()
    }

    /// Get a writable pointer to this writer's memory. Initial contents are unspecified.
    #[no_mangle]
    pub unsafe extern "C" fn process_getWritablePtrRef(
        proc: *mut cshadow::Process,
        plugin_src: cshadow::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRefMut<'static, u8> {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let plugin_src = TypedPluginPtr::new::<u8>(PluginPtr::from(plugin_src), n);
            let memory_ref = memory_manager.memory_ref_mut_uninit(plugin_src);
            match memory_ref {
                Ok(mr) => {
                    // SAFETY: This should only be called from process.c, which
                    // tries to detect incompatible borrows.
                    let mr = unsafe {
                        std::mem::transmute::<
                            ProcessMemoryRefMut<'_, u8>,
                            ProcessMemoryRefMut<'static, u8>,
                        >(mr)
                    };
                    Box::into_raw(Box::new(mr))
                }
                Err(e) => {
                    warn!("Failed to get memory ref: {:?}", e);
                    std::ptr::null_mut()
                }
            }
        })
        .unwrap()
    }

    /// Get a readable and writable pointer to this writer's memory.
    #[no_mangle]
    pub unsafe extern "C" fn _process_getMutablePtr(
        proc: *const RustProcess,
        plugin_src: cshadow::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRefMut<'static, u8> {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let plugin_src = TypedPluginPtr::new::<u8>(PluginPtr::from(plugin_src), n);
            let memory_ref = memory_manager.memory_ref_mut(plugin_src);
            match memory_ref {
                Ok(mr) => {
                    // SAFETY: This should only be called from process.c, which
                    // tries to detect incompatible borrows.
                    let mr = unsafe {
                        std::mem::transmute::<
                            ProcessMemoryRefMut<'_, u8>,
                            ProcessMemoryRefMut<'static, u8>,
                        >(mr)
                    };
                    Box::into_raw(Box::new(mr))
                }
                Err(e) => {
                    warn!("Failed to get memory ref: {:?}", e);
                    std::ptr::null_mut()
                }
            }
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_readString(
        proc: *const RustProcess,
        ptr: cshadow::PluginPtr,
        strbuf: *mut libc::c_char,
        maxlen: libc::size_t,
    ) -> libc::ssize_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let memory_manager = proc.memory_borrow();
            let buf = unsafe {
                std::slice::from_raw_parts_mut(notnull_mut_debug(strbuf) as *mut u8, maxlen)
            };
            let cstr = match memory_manager
                .copy_str_from_ptr(buf, TypedPluginPtr::new::<u8>(PluginPtr::from(ptr), maxlen))
            {
                Ok(cstr) => cstr,
                Err(e) => return -(e as libc::ssize_t),
            };
            cstr.to_bytes().len().try_into().unwrap()
        })
        .unwrap()
    }

    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableShutdownHelper(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            proc.borrow(h.root())
                .descriptor_table_borrow_mut()
                .shutdown_helper();
        })
        .unwrap();
    }

    /// Close all descriptors. The `host` option is a legacy option for legacy files.
    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableRemoveAndCloseAll(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let descriptors = proc
                .borrow(host.root())
                .descriptor_table_borrow_mut()
                .remove_all();
            CallbackQueue::queue_and_run(|cb_queue| {
                for desc in descriptors {
                    desc.close(host, cb_queue);
                }
            });
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
            let mut table = proc.descriptor_table_borrow_mut();
            match table.set(index.try_into().unwrap(), *descriptor.unwrap()) {
                Some(d) => Descriptor::into_raw(Box::new(d)),
                None => std::ptr::null_mut(),
            }
        })
        .unwrap()
    }

    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_createMemoryManager(
        proc: *const RustProcess,
        native_pid: libc::pid_t,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let mman = unsafe { MemoryManager::new(nix::unistd::Pid::from_raw(native_pid)) };
        Worker::with_active_host(move |h| {
            let prev = proc
                .borrow(h.root())
                .memory_manager
                .borrow_mut()
                .replace(mman);
            assert!(prev.is_none());
        })
        .unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getReadablePtrPrefix(
        proc: *const RustProcess,
        plugin_src: cshadow::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRef<'static, u8> {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let memory_manager = proc.memory_borrow();
            match memory_manager
                .memory_ref_prefix(TypedPluginPtr::new::<u8>(PluginPtr::from(plugin_src), n))
            {
                Ok(mr) => {
                    let mr = unsafe { std::mem::transmute::<ProcessMemoryRef<'_, u8>, ProcessMemoryRef<'static, u8>>(mr)};
                    Box::into_raw(Box::new(mr))}
                ,
                Err(e) => {
                    warn!("Couldn't read memory for string: {:?}", e);
                    std::ptr::null_mut()
                }
            }
        }).unwrap()
    }

    /// Fully handles the `mmap` syscall
    #[no_mangle]
    pub unsafe extern "C" fn process_handleMmap(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        addr: cshadow::PluginPtr,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .do_mmap(
                    &mut thread,
                    PluginPtr::from(addr),
                    len,
                    prot,
                    flags,
                    fd,
                    offset,
                )
                .into()
        })
        .unwrap()
    }

    /// Fully handles the `munmap` syscall
    #[no_mangle]
    pub unsafe extern "C" fn process_handleMunmap(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        addr: cshadow::PluginPtr,
        len: usize,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_munmap(&mut thread, PluginPtr::from(addr), len)
                .into()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_handleMremap(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        old_addr: cshadow::PluginPtr,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_addr: cshadow::PluginPtr,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_mremap(
                    &mut thread,
                    PluginPtr::from(old_addr),
                    old_size,
                    new_size,
                    flags,
                    PluginPtr::from(new_addr),
                )
                .into()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_handleMprotect(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        addr: cshadow::PluginPtr,
        size: usize,
        prot: i32,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_mprotect(&mut thread, PluginPtr::from(addr), size, prot)
                .into()
        })
        .unwrap()
    }

    /// Fully handles the `brk` syscall, keeping the "heap" mapped in our shared mem file.
    #[no_mangle]
    pub unsafe extern "C" fn process_handleBrk(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        plugin_src: cshadow::PluginPtr,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_brk(&mut thread, PluginPtr::from(plugin_src))
                .into()
        })
        .unwrap()
    }

    /// Initialize the MemoryMapper if it isn't already initialized. `thread` must
    /// be running and ready to make native syscalls.
    #[no_mangle]
    pub unsafe extern "C" fn process_initMapperIfNeeded(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
    ) {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            if !memory_manager.has_mapper() {
                let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
                memory_manager.init_mapper(&mut thread)
            }
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_resetMemoryManager(proc: *mut cshadow::Process) {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            drop(proc.borrow(h.root()).memory_manager.borrow_mut().take());
        })
        .unwrap()
    }

    /// Returns the processID that was assigned to us in process_new
    ///
    /// Needed for early access from process_new, before there is an active
    /// Host.
    #[no_mangle]
    pub unsafe extern "C" fn _process_getProcessIDWithHost(
        proc: *const RustProcess,
        host: *const Host,
    ) -> libc::pid_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        let host = unsafe { host.as_ref().unwrap() };
        proc.borrow(host.root()).id().try_into().unwrap()
    }

    /// Returns the processID that was assigned to us in process_new
    #[no_mangle]
    pub unsafe extern "C" fn _process_getProcessID(proc: *const RustProcess) -> libc::pid_t {
        Worker::with_active_host(|h| unsafe { _process_getProcessIDWithHost(proc, h) }).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getStartTime(proc: *const RustProcess) -> CEmulatedTime {
        Worker::with_active_host(|host| unsafe { _process_getStartTimeWithHost(proc, host) })
            .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getStartTimeWithHost(
        proc: *const RustProcess,
        host: *const Host,
    ) -> CEmulatedTime {
        let proc = unsafe { proc.as_ref().unwrap() };
        let host = unsafe { host.as_ref().unwrap() };
        let start_time = proc.borrow(host.root()).start_time;
        EmulatedTime::to_c_emutime(Some(start_time))
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getStopTime(proc: *const RustProcess) -> CEmulatedTime {
        Worker::with_active_host(|host| unsafe { _process_getStopTimeWithHost(proc, host) })
            .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getStopTimeWithHost(
        proc: *const RustProcess,
        host: *const Host,
    ) -> CEmulatedTime {
        let proc = unsafe { proc.as_ref().unwrap() };
        let host = unsafe { host.as_ref().unwrap() };
        let stop_time = proc.borrow(host.root()).stop_time;
        EmulatedTime::to_c_emutime(stop_time)
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getHostId(proc: *const RustProcess) -> HostId {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).host_id()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getName(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).name.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getPluginName(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).plugin_name.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getPluginPath(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).plugin_path.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_outputFileName(
        proc: *const RustProcess,
        host: *const Host,
        typ: *const c_char,
        dst: *mut c_char,
        dst_len: usize,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let host = unsafe { host.as_ref().unwrap() };
        let typ = unsafe { CStr::from_ptr(typ).to_str().unwrap() };
        let name = proc.borrow(host.root()).output_file_name(host, typ);
        let name = pathbuf_to_nul_term_cstring(name);
        // XXX: Is this UB? Maybe the bytes at dst are never "undefined" from
        // Rust's perspective since any initialization or lack thereof is
        // invisible through FFI?
        let dst = unsafe { std::slice::from_raw_parts_mut(dst as *mut u8, dst_len) };
        let name = name.as_bytes_with_nul();
        dst[..name.len()].copy_from_slice(name);
    }

    /// Safety:
    ///
    /// The returned pointer is invalidated when the host shmem lock is released, e.g. via
    /// Host::unlock_shmem.
    #[no_mangle]
    pub unsafe extern "C" fn _process_getSharedMem(
        proc: *const RustProcess,
    ) -> *const ShimShmemProcess {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            proc.borrow(host.root()).shim_shared_mem_block.deref() as *const _
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getWorkingDir(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).working_dir.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_straceLoggingMode(proc: *const RustProcess) -> StraceFmtMode {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).strace_logging_mode).unwrap()
    }
}
