use std::collections::{BTreeMap, HashMap};
use std::ffi::CString;
use std::fs::File;
use std::os::fd::AsRawFd;
use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;
use std::process;

use linux_api::errno::Errno;
use linux_api::mman::{MapFlags, ProtFlags};
use log::{debug, trace};
use rustix::fs::MemfdFlags;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::host::context::ThreadContext;
use crate::host::memory_manager::AllocdMem;
use crate::host::process::ProcessId;

const PERMISSION_MASK: i32 = 0o777;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Attachment {
    shmid: i32,
    addr: usize,
}

#[derive(Debug)]
struct Segment {
    id: i32,
    key: libc::key_t,
    size: usize,
    mode: u16,
    uid: libc::uid_t,
    gid: libc::gid_t,
    ctime: libc::time_t,
    atime: libc::time_t,
    dtime: libc::time_t,
    cpid: libc::pid_t,
    lpid: libc::pid_t,
    removed: bool,
    attach_count: usize,
    file: File,
}

impl Segment {
    fn new(id: i32, key: libc::key_t, size: usize, shmflg: i32, creator_pid: ProcessId) -> Self {
        let creator_uid = nix::unistd::geteuid().as_raw();
        let creator_gid = nix::unistd::getegid().as_raw();
        let mode = (shmflg & PERMISSION_MASK).try_into().unwrap();
        let now = current_time_t();
        let shm_name = CString::new(format!("shadow_sysv_shm_{}_{}", process::id(), id)).unwrap();
        let raw_file = rustix::fs::memfd_create(&shm_name, MemfdFlags::CLOEXEC).unwrap();
        let file = File::from(raw_file);
        rustix::fs::ftruncate(&file, size.try_into().unwrap()).unwrap();

        Self {
            id,
            key,
            size,
            mode,
            uid: creator_uid,
            gid: creator_gid,
            ctime: now,
            atime: 0,
            dtime: 0,
            cpid: creator_pid.into(),
            lpid: 0,
            removed: false,
            attach_count: 0,
            file,
        }
    }

    fn can_attach(&self, write: bool) -> bool {
        let euid = nix::unistd::geteuid().as_raw();
        let egid = nix::unistd::getegid().as_raw();

        if euid == 0 || euid == self.uid {
            return true;
        }

        let access_mask = if write { 0o2 } else { 0o4 };
        let shifted_mode = if egid == self.gid {
            self.mode >> 3
        } else {
            self.mode
        };

        (shifted_mode & access_mask) == access_mask
    }

    fn open_in_plugin(&self, ctx: &ThreadContext, read_only: bool) -> Result<i32, Errno> {
        let path = persistent_shadow_fd_path(self.file.as_raw_fd());
        let path_bytes = path.as_os_str().as_bytes();

        let plugin_buffer = AllocdMem::<u8>::new(ctx, path_bytes.len() + 1);
        {
            let mut memory = ctx.process.memory_borrow_mut();
            memory.copy_to_ptr(plugin_buffer.ptr().slice(..path_bytes.len()), path_bytes)?;
            memory.copy_to_ptr(plugin_buffer.ptr().slice(path_bytes.len()..), &[0])?;
        }

        let flags = if read_only {
            libc::O_RDONLY | libc::O_CLOEXEC
        } else {
            libc::O_RDWR | libc::O_CLOEXEC
        };

        let (process_ctx, thread) = ctx.split_thread();
        let rv = thread.native_open(&process_ctx, plugin_buffer.ptr().ptr(), flags, 0);
        plugin_buffer.free(ctx);
        rv
    }

    fn to_shmid_ds(&self) -> libc::shmid_ds {
        let mut ds: libc::shmid_ds = unsafe { std::mem::zeroed() };
        ds.shm_perm.__key = self.key;
        ds.shm_perm.uid = self.uid;
        ds.shm_perm.gid = self.gid;
        ds.shm_perm.cuid = self.uid;
        ds.shm_perm.cgid = self.gid;
        ds.shm_perm.mode = self.mode.into();
        ds.shm_perm.__seq = 0;
        ds.shm_segsz = self.size;
        ds.shm_atime = self.atime;
        ds.shm_dtime = self.dtime;
        ds.shm_ctime = self.ctime;
        ds.shm_cpid = self.cpid;
        ds.shm_lpid = self.lpid;
        ds.shm_nattch = self.attach_count.try_into().unwrap_or(libc::shmatt_t::MAX);
        ds
    }
}

#[derive(Debug, Default)]
pub struct SysvShmNamespace {
    segments_by_id: HashMap<i32, Segment>,
    segments_by_key: HashMap<libc::key_t, i32>,
    attachments_by_process: HashMap<ProcessId, BTreeMap<usize, Attachment>>,
    next_id: i32,
}

impl SysvShmNamespace {
    pub fn new() -> Self {
        Self {
            next_id: 1,
            ..Default::default()
        }
    }

    pub fn shmget(
        &mut self,
        process_id: ProcessId,
        key: libc::key_t,
        size: usize,
        shmflg: i32,
    ) -> Result<i32, Errno> {
        let create = (shmflg & libc::IPC_CREAT) != 0;
        let exclusive = (shmflg & libc::IPC_EXCL) != 0;

        if key == libc::IPC_PRIVATE {
            if size == 0 {
                return Err(Errno::EINVAL);
            }
            return Ok(self.create_segment(process_id, key, size, shmflg));
        }

        if let Some(shmid) = self.segments_by_key.get(&key).copied() {
            let segment = self.segments_by_id.get(&shmid).unwrap();

            if create && exclusive {
                return Err(Errno::EEXIST);
            }
            if size > 0 && size > segment.size {
                return Err(Errno::EINVAL);
            }

            let wants_write = (shmflg & 0o222) != 0;
            if !segment.can_attach(wants_write) {
                return Err(Errno::EACCES);
            }

            return Ok(shmid);
        }

        if !create {
            return Err(Errno::ENOENT);
        }
        if size == 0 {
            return Err(Errno::EINVAL);
        }

        Ok(self.create_segment(process_id, key, size, shmflg))
    }

    pub fn shmat(
        &mut self,
        ctx: &ThreadContext,
        shmid: i32,
        shmaddr: ForeignPtr<u8>,
        shmflg: i32,
    ) -> Result<ForeignPtr<u8>, Errno> {
        if !shmaddr.is_null() {
            debug!("shmat with non-NULL address is not implemented");
            return Err(Errno::EINVAL);
        }

        let read_only = (shmflg & libc::SHM_RDONLY) != 0;
        let segment = self.segments_by_id.get(&shmid).ok_or(Errno::EINVAL)?;
        if !segment.can_attach(!read_only) {
            return Err(Errno::EACCES);
        }

        let plugin_fd = segment.open_in_plugin(ctx, read_only)?;
        let prot = if read_only {
            ProtFlags::PROT_READ
        } else {
            ProtFlags::PROT_READ | ProtFlags::PROT_WRITE
        };
        let addr = ctx.process.memory_borrow_mut().do_mmap(
            ctx,
            ForeignPtr::null(),
            segment.size,
            prot,
            MapFlags::MAP_SHARED,
            plugin_fd,
            0,
        );
        close_plugin_fd(ctx, plugin_fd);

        let addr = addr?;
        let attachment = Attachment {
            shmid,
            addr: usize::from(addr),
        };
        let process_attachments = self
            .attachments_by_process
            .entry(ctx.process.id())
            .or_default();
        if process_attachments
            .insert(attachment.addr, attachment)
            .is_some()
        {
            return Err(Errno::EINVAL);
        }

        let segment = self.segments_by_id.get_mut(&shmid).unwrap();
        segment.attach_count += 1;
        segment.atime = current_time_t();
        segment.lpid = ctx.process.id().into();

        Ok(addr)
    }

    pub fn shmdt(&mut self, ctx: &ThreadContext, shmaddr: ForeignPtr<u8>) -> Result<(), Errno> {
        let attachment = self
            .attachments_by_process
            .get(&ctx.process.id())
            .and_then(|attachments| attachments.get(&usize::from(shmaddr)).copied())
            .ok_or(Errno::EINVAL)?;

        let size = self
            .segments_by_id
            .get(&attachment.shmid)
            .map(|segment| segment.size)
            .ok_or(Errno::EINVAL)?;
        ctx.process
            .memory_borrow_mut()
            .do_munmap(ctx, shmaddr, size)?;

        self.detach_by_address(ctx.process.id(), attachment.addr);
        Ok(())
    }

    pub fn shmctl_ipc_stat(&self, shmid: i32) -> Result<libc::shmid_ds, Errno> {
        let segment = self.segments_by_id.get(&shmid).ok_or(Errno::EINVAL)?;
        Ok(segment.to_shmid_ds())
    }

    pub fn shmctl_ipc_rmid(&mut self, shmid: i32) -> Result<(), Errno> {
        let (key, remove_now) = {
            let segment = self.segments_by_id.get_mut(&shmid).ok_or(Errno::EINVAL)?;
            segment.removed = true;
            (segment.key, segment.attach_count == 0)
        };
        if key != libc::IPC_PRIVATE {
            self.segments_by_key.remove(&key);
        }
        if remove_now {
            self.segments_by_id.remove(&shmid);
        }
        Ok(())
    }

    pub fn detach_all_process(&mut self, process_id: ProcessId) {
        let Some(attachments) = self.attachments_by_process.remove(&process_id) else {
            return;
        };

        for attachment in attachments.into_values() {
            self.detach_segment(process_id, attachment.shmid);
        }
    }

    pub fn fork_process(&mut self, parent_pid: ProcessId, child_pid: ProcessId) {
        let Some(parent_attachments) = self.attachments_by_process.get(&parent_pid).cloned() else {
            return;
        };

        for attachment in parent_attachments.into_values() {
            let inserted = {
                let child_attachments = self.attachments_by_process.entry(child_pid).or_default();
                child_attachments
                    .insert(attachment.addr, attachment)
                    .is_none()
            };
            if inserted && let Some(segment) = self.segments_by_id.get_mut(&attachment.shmid) {
                segment.attach_count += 1;
            }
        }
    }

    fn create_segment(
        &mut self,
        process_id: ProcessId,
        key: libc::key_t,
        size: usize,
        shmflg: i32,
    ) -> i32 {
        let shmid = self.next_id;
        self.next_id += 1;

        let segment = Segment::new(shmid, key, size, shmflg, process_id);
        if key != libc::IPC_PRIVATE {
            self.segments_by_key.insert(key, shmid);
        }
        self.segments_by_id.insert(shmid, segment);
        shmid
    }

    fn detach_by_address(&mut self, process_id: ProcessId, addr: usize) {
        let shmid = {
            let attachments = self.attachments_by_process.get_mut(&process_id).unwrap();
            let shmid = attachments.remove(&addr).unwrap().shmid;
            let remove_process_entry = attachments.is_empty();
            (shmid, remove_process_entry)
        };
        if shmid.1 {
            self.attachments_by_process.remove(&process_id);
        }
        self.detach_segment(process_id, shmid.0);
    }

    fn detach_segment(&mut self, process_id: ProcessId, shmid: i32) {
        let Some(segment) = self.segments_by_id.get_mut(&shmid) else {
            return;
        };

        segment.attach_count = segment.attach_count.saturating_sub(1);
        segment.dtime = current_time_t();
        segment.lpid = process_id.into();
        let segment_id = segment.id;
        let attach_count = segment.attach_count;
        let remove_now = segment.removed && segment.attach_count == 0;
        trace!(
            "Detached SysV shm segment {} from process {}; remaining attachments={}",
            segment_id, process_id, attach_count
        );

        if remove_now {
            self.segments_by_id.remove(&shmid);
        }
    }
}

fn current_time_t() -> libc::time_t {
    let now = Worker::current_time().unwrap_or(EmulatedTime::SIMULATION_START);
    now.duration_since(&EmulatedTime::UNIX_EPOCH)
        .as_secs()
        .try_into()
        .unwrap_or(libc::time_t::MAX)
}

fn persistent_shadow_fd_path(fd: i32) -> PathBuf {
    ["/proc", &process::id().to_string(), "fd", &fd.to_string()]
        .iter()
        .collect()
}

fn close_plugin_fd(ctx: &ThreadContext, plugin_fd: i32) {
    let (process_ctx, thread) = ctx.split_thread();
    if let Err(e) = thread.native_close(&process_ctx, plugin_fd) {
        debug!("Failed to close plugin SysV shm fd {plugin_fd}: {e}");
    }
}
