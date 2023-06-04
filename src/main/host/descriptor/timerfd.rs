use std::io::Write;

use nix::errno::Errno;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::{
    FileMode, FileState, FileStatus, StateEventSource, StateListenerFilter,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::{IoVec, IoVecWriter};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::host::timer::Timer;
use crate::utility::callback_queue::{CallbackQueue, Handle};
use crate::utility::HostTreePointer;

pub struct TimerFd {
    timer: Timer,
    event_source: StateEventSource,
    state: FileState,
    status: FileStatus,
    // Should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file,
    has_open_file: bool,
}

impl TimerFd {
    pub fn new() -> Self {
        todo!()
    }

    pub fn get_status(&self) -> FileStatus {
        self.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.status = status;
    }

    pub fn mode(&self) -> FileMode {
        FileMode::READ
    }

    pub fn has_open_file(&self) -> bool {
        self.has_open_file
    }

    pub fn supports_sa_restart(&self) -> bool {
        false
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.has_open_file = val;
    }

    pub fn readv(
        &mut self,
        iovs: &[IoVec],
        offset: Option<libc::off_t>,
        _flags: libc::c_int,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // TimerFds don't support seeking
        if offset.is_some() {
            return Err(Errno::ESPIPE.into());
        }

        // timerfd_create(2): "read(2) returns an unsigned 8-byte integer containing the number of
        // expirations that have occurred."
        const NUM_BYTES: usize = 8;

        let len: libc::size_t = iovs.iter().map(|x| x.len).sum();

        // This check doesn't guarantee that we can write all bytes since the stream length is only
        // a hint.
        if len < NUM_BYTES {
            log::trace!(
                "Reading from TimerFd requires a buffer of at least {} bytes",
                NUM_BYTES
            );
            return Err(Errno::EINVAL.into());
        }

        let expiration_count = self.timer.consume_expiration_count();

        if expiration_count == 0 {
            log::trace!("TimerFd expiration count is 0 and cannot be read right now");
            return Err(Errno::EWOULDBLOCK.into());
        }

        let mut writer = IoVecWriter::new(iovs, mem);
        let to_write: [u8; NUM_BYTES] = expiration_count.to_ne_bytes();
        writer.write_all(&to_write)?;

        // We just read the expiration counter and so are not readable anymore.
        self.update_state(cb_queue);

        Ok(NUM_BYTES.try_into().unwrap())
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // TimerFds don't support writing.
        Err(Errno::EINVAL.into())
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        // Set the closed flag and remove the active and readable flags.
        self.copy_state(
            FileState::CLOSED | FileState::ACTIVE | FileState::READABLE,
            FileState::CLOSED,
            cb_queue,
        );

        Ok(())
    }

    pub fn ioctl(
        &mut self,
        request: u64,
        _arg_ptr: ForeignPtr<()>,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        // The only timerfd-specific ioctl request is for `TFD_IOC_SET_TICKS`, which is available
        // since Linux 3.17 but only if the kernel was configured with `CONFIG_CHECKPOINT_RESTORE`.
        // See timerfd_create(2) for more details.
        log::warn!("We do not yet handle ioctl request {} on TimerFds", request);
        Err(Errno::EINVAL.into())
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.state
    }

    fn update_state(&mut self, cb_queue: &mut CallbackQueue) {
        if self.state.contains(FileState::CLOSED) {
            return;
        }

        let mut new_state = FileState::empty();

        // Set the descriptor as readable if we have a non-zero expiration count.
        new_state.set(FileState::READABLE, self.get_expiration_count() > 0);

        self.copy_state(FileState::READABLE, new_state, cb_queue);
    }

    fn copy_state(&mut self, mask: FileState, state: FileState, cb_queue: &mut CallbackQueue) {
        let old_state = self.state;

        // Remove the mask, then copy the masked flags.
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, cb_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, cb_queue: &mut CallbackQueue) {
        let states_changed = self.state ^ old_state;

        // Just return if nothing changed.
        if states_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, cb_queue);
    }

    fn get_expiration_count(&self) -> u64 {
        self.timer.expiration_count()
    }

    pub fn get_time(&self) -> SimulationTime {
        todo!()
    }

    pub fn set_time(
        &self,
        _new_time: SimulationTime,
    ) -> Result<Option<SimulationTime>, SyscallError> {
        todo!()
    }
}
