use atomic_refcell::AtomicRefCell;
use std::sync::Arc;

use crate::cshadow as c;
use crate::host::descriptor::{
    FileMode, NewStatusListenerFilter, PosixFile, StatusEventSource, SyscallReturn,
};
use crate::utility::byte_queue::ByteQueue;
use crate::utility::event_queue::{EventQueue, Handle};

pub struct PipeFile {
    buffer: Arc<AtomicRefCell<SharedBuf>>,
    event_source: StatusEventSource,
    status: c::Status,
    mode: FileMode,
    buffer_event_handle: Option<Handle<(c::Status, c::Status)>>,
}

impl PipeFile {
    pub fn new(buffer: Arc<AtomicRefCell<SharedBuf>>, mode: FileMode) -> Self {
        let mut rv = Self {
            buffer,
            event_source: StatusEventSource::new(),
            status: c::_Status_STATUS_NONE,
            mode,
            buffer_event_handle: None,
        };

        rv.status = rv.filter_status(rv.buffer.borrow_mut().status());

        rv
    }

    pub fn read(&mut self, bytes: &mut [u8], event_queue: &mut EventQueue) -> SyscallReturn {
        // if the file is not open for reading, return EBADF
        if !self.mode.contains(FileMode::READ) {
            return SyscallReturn::Error(nix::errno::Errno::EBADF);
        }

        let num_read = {
            let mut buffer = self.buffer.borrow_mut();
            buffer.read(bytes, event_queue)
        };

        SyscallReturn::Success(num_read as i32)
    }

    pub fn write(&mut self, bytes: &[u8], event_queue: &mut EventQueue) -> SyscallReturn {
        // if the file is not open for writing, return EBADF
        if !self.mode.contains(FileMode::WRITE) {
            return SyscallReturn::Error(nix::errno::Errno::EBADF);
        }

        let count = {
            let mut buffer = self.buffer.borrow_mut();
            buffer.write(bytes, event_queue)
        };

        // the write would block if we could not write any bytes, but were asked to
        if count == 0 && bytes.len() > 0 {
            SyscallReturn::Error(nix::errno::EWOULDBLOCK)
        } else {
            SyscallReturn::Success(count as i32)
        }
    }

    pub fn enable_notifications(arc: &Arc<AtomicRefCell<PosixFile>>) {
        // we remove some of these later in this function
        let monitoring = c::_Status_STATUS_DESCRIPTOR_ACTIVE
            | c::_Status_STATUS_DESCRIPTOR_CLOSED
            | c::_Status_STATUS_DESCRIPTOR_READABLE
            | c::_Status_STATUS_DESCRIPTOR_WRITABLE;

        let weak = Arc::downgrade(arc);
        match *arc.borrow_mut() {
            PosixFile::Pipe(ref mut f) => {
                // remove any status flags that aren't relevant to us
                let monitoring = f.filter_status(monitoring);

                f.buffer_event_handle = Some(f.buffer.borrow_mut().add_listener(
                    monitoring,
                    NewStatusListenerFilter::Always,
                    move |status, _changed, event_queue| {
                        // if the file hasn't been dropped
                        if let Some(file) = weak.upgrade() {
                            let mut file = file.borrow_mut();
                            match *file {
                                PosixFile::Pipe(ref mut f) => {
                                    f.adjust_status(status, true, event_queue)
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
        monitoring: c::Status,
        filter: NewStatusListenerFilter,
        notify_fn: impl Fn(c::Status, c::Status, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(c::Status, c::Status)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn status(&self) -> c::Status {
        self.status
    }

    fn filter_status(&self, mut status: c::Status) -> c::Status {
        // if not open for reading
        if !self.mode.contains(FileMode::READ) {
            // remove the readable flag
            status &= !c::_Status_STATUS_DESCRIPTOR_READABLE;
        }

        // if not open for writing
        if !self.mode.contains(FileMode::WRITE) {
            // remove the writable flag
            status &= !c::_Status_STATUS_DESCRIPTOR_WRITABLE;
        }

        status
    }

    pub fn adjust_status(
        &mut self,
        status: c::Status,
        do_set_bits: bool,
        event_queue: &mut EventQueue,
    ) {
        let old_status = self.status;

        // remove any flags that aren't relevant
        let status = self.filter_status(status);

        if do_set_bits {
            self.status |= status;
        } else {
            self.status &= !status;
        }

        self.handle_status_change(old_status, event_queue);
    }

    fn handle_status_change(&mut self, old_status: c::Status, event_queue: &mut EventQueue) {
        let statuses_changed = self.status ^ old_status;

        // if nothing changed
        if statuses_changed == 0 {
            return;
        }

        self.event_source
            .notify_listeners(self.status, statuses_changed, event_queue);
    }
}

pub struct SharedBuf {
    queue: ByteQueue,
    max_len: usize,
    status: c::Status,
    event_source: StatusEventSource,
}

impl SharedBuf {
    pub fn new() -> Self {
        Self {
            queue: ByteQueue::new(8192),
            max_len: c::CONFIG_PIPE_BUFFER_SIZE as usize,
            status: c::_Status_STATUS_DESCRIPTOR_ACTIVE | c::_Status_STATUS_DESCRIPTOR_WRITABLE,
            event_source: StatusEventSource::new(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.queue.len() == 0
    }

    pub fn space_available(&self) -> usize {
        self.max_len - self.queue.len()
    }

    pub fn read(&mut self, bytes: &mut [u8], event_queue: &mut EventQueue) -> usize {
        let num = self.queue.pop(bytes);

        // readable if not empty
        self.adjust_status(
            c::_Status_STATUS_DESCRIPTOR_READABLE,
            !self.is_empty(),
            event_queue,
        );

        // writable if space is available
        self.adjust_status(
            c::_Status_STATUS_DESCRIPTOR_WRITABLE,
            self.space_available() > 0,
            event_queue,
        );

        num
    }

    pub fn write(&mut self, bytes: &[u8], event_queue: &mut EventQueue) -> usize {
        let available = self.space_available();
        let writable = &bytes[..std::cmp::min(bytes.len(), available)];
        self.queue.push(writable);

        self.adjust_status(
            c::_Status_STATUS_DESCRIPTOR_READABLE,
            !self.is_empty(),
            event_queue,
        );
        self.adjust_status(
            c::_Status_STATUS_DESCRIPTOR_WRITABLE,
            self.space_available() > 0,
            event_queue,
        );

        writable.len()
    }

    pub fn add_listener(
        &mut self,
        monitoring: c::Status,
        filter: NewStatusListenerFilter,
        notify_fn: impl Fn(c::Status, c::Status, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(c::Status, c::Status)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn status(&self) -> c::Status {
        self.status
    }

    fn adjust_status(
        &mut self,
        status: c::Status,
        do_set_bits: bool,
        event_queue: &mut EventQueue,
    ) {
        let old_status = self.status;

        if do_set_bits {
            self.status |= status;
        } else {
            self.status &= !status;
        }

        self.handle_status_change(old_status, event_queue);
    }

    fn handle_status_change(&mut self, old_status: c::Status, event_queue: &mut EventQueue) {
        let statuses_changed = self.status ^ old_status;

        // if nothing changed
        if statuses_changed == 0 {
            return;
        }

        self.event_source
            .notify_listeners(self.status, statuses_changed, event_queue);
    }
}
