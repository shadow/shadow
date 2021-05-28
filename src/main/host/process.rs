use nix::unistd::Pid;

use crate::cshadow;

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
    /// For now, this should only be called via Worker, to borrow the current
    /// Process. This will ensure there is only one reference to a given Process
    /// in Rust.
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
        let mm_ptr: *mut cshadow::MemoryManager =
            unsafe { cshadow::process_getMemoryManager(self.cprocess) };
        // MemoryManager and cshadow::MemoryManager are the same type.
        unsafe { std::mem::transmute(mm_ptr) }
    }

    pub fn memory_mut(&mut self) -> &mut MemoryManager {
        unsafe { &mut *self.memory_manager_ptr() }
    }

    pub fn memory(&self) -> &MemoryManager {
        unsafe { &*self.memory_manager_ptr() }
    }

    pub fn native_pid(&self) -> Pid {
        let pid = unsafe { cshadow::process_getNativePid(self.cprocess) };
        Pid::from_raw(pid)
    }

    pub fn raw_mut(&mut self) -> *mut cshadow::Process {
        self.cprocess
    }
}
