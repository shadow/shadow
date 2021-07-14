use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;
use std::sync::Arc;

use crate::cshadow as c;
use crate::host::descriptor::{
    FileFlags, FileMode, FileStatus, NewStatusListenerFilter, PosixFile, StatusEventSource,
};
use crate::host::syscall_types::SyscallResult;
use crate::utility::byte_queue::ByteQueue;
use crate::utility::event_queue::{EventQueue, Handle};
use crate::utility::stream_len::StreamLen;

pub struct PipeFile {
    buffer: Arc<AtomicRefCell<SharedBuf>>,
    event_source: StatusEventSource,
    status: FileStatus,
    mode: FileMode,
    flags: FileFlags,
    // we only store this so that the handle is dropped when we are
    _buffer_event_handle: Option<Handle<(FileStatus, FileStatus)>>,
}

impl PipeFile {
    pub fn new(buffer: Arc<AtomicRefCell<SharedBuf>>, mode: FileMode, flags: FileFlags) -> Self {
        let mut rv = Self {
            buffer,
            event_source: StatusEventSource::new(),
            status: FileStatus::ACTIVE,
            mode,
            flags,
            _buffer_event_handle: None,
        };

        rv.status
            .insert(rv.filter_status(rv.buffer.borrow_mut().status()));

        rv
    }

    pub fn get_flags(&self) -> FileFlags {
        self.flags
    }

    pub fn set_flags(&mut self, flags: FileFlags) {
        self.flags = flags;
    }

    pub fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult {
        // set the closed flag and remove the active flag
        self.copy_status(
            FileStatus::CLOSED | FileStatus::ACTIVE,
            FileStatus::CLOSED,
            event_queue,
        );
        Ok(0.into())
    }

    pub fn read<W>(
        &mut self,
        bytes: W,
        offset: libc::off_t,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        W: std::io::Write + std::io::Seek,
    {
        // pipes don't support seeking
        if offset != 0 {
            return Err(nix::errno::Errno::ESPIPE.into());
        }

        // if the file is not open for reading, return EBADF
        if !self.mode.contains(FileMode::READ) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let mut bytes = bytes;
        let num_read = self.buffer.borrow_mut().read(&mut bytes, event_queue)?;

        // the read would block if we could not write any bytes, but were asked to
        if usize::from(num_read) == 0 && bytes.stream_len_bp()? != 0 {
            Err(Errno::EWOULDBLOCK.into())
        } else {
            Ok(num_read.into())
        }
    }

    pub fn write<R>(
        &mut self,
        bytes: R,
        offset: libc::off_t,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        // pipes don't support seeking
        if offset != 0 {
            return Err(nix::errno::Errno::ESPIPE.into());
        }

        // if the file is not open for writing, return EBADF
        if !self.mode.contains(FileMode::WRITE) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let mut bytes = bytes;
        let num_written = self
            .buffer
            .borrow_mut()
            .write(bytes.by_ref(), event_queue)?;

        // the write would block if we could not write any bytes, but were asked to
        if usize::from(num_written) == 0 && bytes.stream_len_bp()? != 0 {
            Err(Errno::EWOULDBLOCK.into())
        } else {
            Ok(num_written.into())
        }
    }

    pub fn enable_notifications(arc: &Arc<AtomicRefCell<PosixFile>>) {
        // we remove some of these later in this function
        let monitoring = FileStatus::READABLE | FileStatus::WRITABLE;

        let weak = Arc::downgrade(arc);
        match *arc.borrow_mut() {
            PosixFile::Pipe(ref mut f) => {
                // remove any status flags that aren't relevant to us
                let monitoring = f.filter_status(monitoring);

                f._buffer_event_handle = Some(f.buffer.borrow_mut().add_listener(
                    monitoring,
                    NewStatusListenerFilter::Always,
                    move |status, _changed, event_queue| {
                        // if the file hasn't been dropped
                        if let Some(file) = weak.upgrade() {
                            let mut file = file.borrow_mut();
                            match *file {
                                PosixFile::Pipe(ref mut f) => {
                                    f.copy_status(monitoring, status, event_queue)
                                }
                                #[allow(unreachable_patterns)]
                                _ => unreachable!(),
                            }
                        }
                    },
                ));
            }
            #[allow(unreachable_patterns)]
            _ => unreachable!(),
        };
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileStatus,
        filter: NewStatusListenerFilter,
        notify_fn: impl Fn(FileStatus, FileStatus, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(FileStatus, FileStatus)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn status(&self) -> FileStatus {
        self.status
    }

    fn filter_status(&self, mut status: FileStatus) -> FileStatus {
        // if not open for reading, remove the readable flag
        if !self.mode.contains(FileMode::READ) {
            status.remove(FileStatus::READABLE);
        }

        // if not open for writing, remove the writable flag
        if !self.mode.contains(FileMode::WRITE) {
            status.remove(FileStatus::WRITABLE);
        }

        status
    }

    fn copy_status(&mut self, mask: FileStatus, status: FileStatus, event_queue: &mut EventQueue) {
        let old_status = self.status;

        // remove any flags that aren't relevant
        let status = self.filter_status(status);

        // remove the masked flags, then copy the masked flags
        self.status.remove(mask);
        self.status.insert(status & mask);

        self.handle_status_change(old_status, event_queue);
    }

    fn handle_status_change(&mut self, old_status: FileStatus, event_queue: &mut EventQueue) {
        let statuses_changed = self.status ^ old_status;

        // if nothing changed
        if statuses_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.status, statuses_changed, event_queue);
    }
}

pub struct SharedBuf {
    queue: ByteQueue,
    max_len: usize,
    status: FileStatus,
    event_source: StatusEventSource,
}

impl SharedBuf {
    pub fn new() -> Self {
        Self {
            queue: ByteQueue::new(8192),
            max_len: c::CONFIG_PIPE_BUFFER_SIZE as usize,
            status: FileStatus::WRITABLE,
            event_source: StatusEventSource::new(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.queue.len() == 0
    }

    pub fn space_available(&self) -> usize {
        self.max_len - self.queue.len()
    }

    pub fn read<W: std::io::Write>(
        &mut self,
        bytes: W,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let num = self.queue.pop(bytes)?;

        // readable if not empty
        self.adjust_status(FileStatus::READABLE, !self.is_empty(), event_queue);

        // writable if space is available
        self.adjust_status(
            FileStatus::WRITABLE,
            self.space_available() > 0,
            event_queue,
        );

        Ok(num.into())
    }

    pub fn write<R: std::io::Read>(
        &mut self,
        bytes: R,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let written = self.queue.push(bytes.take(self.space_available() as u64))?;

        self.adjust_status(FileStatus::READABLE, !self.is_empty(), event_queue);
        self.adjust_status(
            FileStatus::WRITABLE,
            self.space_available() > 0,
            event_queue,
        );

        Ok(written.into())
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileStatus,
        filter: NewStatusListenerFilter,
        notify_fn: impl Fn(FileStatus, FileStatus, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(FileStatus, FileStatus)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn status(&self) -> FileStatus {
        self.status
    }

    fn adjust_status(
        &mut self,
        status: FileStatus,
        do_set_bits: bool,
        event_queue: &mut EventQueue,
    ) {
        let old_status = self.status;

        // add or remove the flags
        self.status.set(status, do_set_bits);

        self.handle_status_change(old_status, event_queue);
    }

    fn handle_status_change(&mut self, old_status: FileStatus, event_queue: &mut EventQueue) {
        let statuses_changed = self.status ^ old_status;

        // if nothing changed
        if statuses_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.status, statuses_changed, event_queue);
    }
}
