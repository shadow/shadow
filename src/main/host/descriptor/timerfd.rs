use std::io::Write;
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use linux_api::posix_types::kernel_off_t;
use shadow_shim_helper_rs::{
    emulated_time::EmulatedTime, simulation_time::SimulationTime, syscall_types::ForeignPtr,
};

use crate::cshadow as c;
use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::{FileMode, FileSignals, FileState, FileStatus};
use crate::host::host::Host;
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::{IoVec, IoVecWriter};
use crate::host::syscall::types::{SyscallError, SyscallResult};
use crate::host::timer::Timer;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::HostTreePointer;

pub struct TimerFd {
    timer: Timer,
    event_source: StateEventSource,
    status: FileStatus,
    state: FileState,
    // Should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file,
    has_open_file: bool,
}

impl TimerFd {
    /// Creates a new [`TimerFd`] object that internally sets up a [`Timer`] that can be waited on
    /// with poll, select, and epoll, enabling support for timerfd_create(2).
    ///
    /// We wrap the new [`TimerFd`] in an [`Arc<AtomicRefCell>`] because we need to use a weak
    /// reference to internally support setting up callback functions that reference the [`TimerFd`]
    /// on timer expiration.
    pub fn new(status: FileStatus) -> Arc<AtomicRefCell<Self>> {
        // We need a circular reference here, so that the inner Timer can refer back to the outer
        // TimerFd when executing a callback that will mutate the TimerFd when the timer expires.
        Arc::new_cyclic(|weak| {
            let weak_cloned = weak.clone();
            AtomicRefCell::new(Self {
                timer: Timer::new(move |_host| Self::timer_expired(&weak_cloned)),
                event_source: StateEventSource::new(),
                state: FileState::ACTIVE,
                status,
                has_open_file: false,
            })
        })
    }

    /// Called by the inner [`Timer`] when a timer expiration occurs.
    fn timer_expired(timerfd_weak: &Weak<AtomicRefCell<TimerFd>>) {
        let Some(timerfd) = timerfd_weak.upgrade() else {
            log::trace!("Expired TimerFd no longer exists.");
            return;
        };

        // The TimerFd may have become readable now that a timer expired. We use the CallbackQueue
        // here to make sure that any listeners that need to wake up and handle a readable TimerFd
        // are not invoked until after we release the borrow.
        CallbackQueue::queue_and_run(|cb_queue| {
            timerfd.borrow_mut().refresh_state(cb_queue);
        });
    }

    /// Returns the number of expirations that have occured since the timer was last armed.
    fn get_timer_count(&self) -> u64 {
        self.timer.expiration_count()
    }

    /// Returns the relative duration until the next expiration event occurs if the timer is armed,
    /// and `None` if the timer is disarmed.
    pub fn get_timer_remaining(&self) -> Option<SimulationTime> {
        self.timer.remaining_time()
    }

    /// Returns the relative duration over which the timer has been configured to periodically
    /// expire, or `None` if the timer is configured to expire only once.
    pub fn get_timer_interval(&self) -> Option<SimulationTime> {
        self.timer.expire_interval()
    }

    /// Arm the timer by setting its expiration time and interval, enabling support for
    /// timerfd_settime(2). The readable state of the [`TimerFd`] is updated as appropriate.
    pub fn arm_timer(
        &mut self,
        host: &Host,
        expire_time: EmulatedTime,
        interval: Option<SimulationTime>,
        cb_queue: &mut CallbackQueue,
    ) {
        // Make sure to update our READABLE status.
        self.timer.arm(host, expire_time, interval);
        self.refresh_state(cb_queue);
    }

    /// Disarm the timer so that it no longer fires expiration events, enabling support for
    /// timerfd_settime(2). The readable state of the [`TimerFd`] is updated as appropriate.
    pub fn disarm_timer(&mut self, cb_queue: &mut CallbackQueue) {
        // Make sure to update our READABLE status.
        self.timer.disarm();
        self.refresh_state(cb_queue);
    }

    pub fn status(&self) -> FileStatus {
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
        offset: Option<kernel_off_t>,
        _flags: std::ffi::c_int,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<isize, SyscallError> {
        // TimerFds don't support seeking
        if offset.is_some() {
            return Err(Errno::ESPIPE.into());
        }

        // timerfd_create(2): "read(2) returns an unsigned 8-byte integer containing the number of
        // expirations that have occurred."
        const NUM_BYTES: usize = 8;

        let len: usize = iovs.iter().map(|x| x.len).sum();

        // This check doesn't guarantee that we can write all bytes since the stream length is only
        // a hint.
        if len < NUM_BYTES {
            log::trace!("Reading from TimerFd requires a buffer of at least {NUM_BYTES} bytes",);
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
        self.refresh_state(cb_queue);

        Ok(NUM_BYTES.try_into().unwrap())
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<kernel_off_t>,
        _flags: std::ffi::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<isize, SyscallError> {
        // TimerFds don't support writing.
        Err(Errno::EINVAL.into())
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        // Set the closed flag and remove the active and readable flags.
        self.update_state(
            FileState::CLOSED | FileState::ACTIVE | FileState::READABLE,
            FileState::CLOSED,
            FileSignals::empty(),
            cb_queue,
        );

        Ok(())
    }

    pub fn ioctl(
        &mut self,
        request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        // The only timerfd-specific ioctl request is for `TFD_IOC_SET_TICKS`, which is available
        // since Linux 3.17 but only if the kernel was configured with `CONFIG_CHECKPOINT_RESTORE`.
        // See timerfd_create(2) for more details.
        warn_once_then_debug!("We do not yet handle ioctl request {request:?} on TimerFds");
        Err(Errno::EINVAL.into())
    }

    pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError> {
        warn_once_then_debug!("We do not yet handle stat calls on timerfds");
        Err(Errno::EINVAL.into())
    }

    pub fn add_listener(
        &mut self,
        monitoring_state: FileState,
        monitoring_signals: FileSignals,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, FileSignals, &mut CallbackQueue)
            + Send
            + Sync
            + 'static,
    ) -> StateListenHandle {
        self.event_source
            .add_listener(monitoring_state, monitoring_signals, filter, notify_fn)
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

    fn refresh_state(&mut self, cb_queue: &mut CallbackQueue) {
        if self.state.contains(FileState::CLOSED) {
            return;
        }

        let mut new_state = FileState::empty();

        // Set the descriptor as readable if we have a non-zero expiration count.
        new_state.set(FileState::READABLE, self.get_timer_count() > 0);

        self.update_state(
            FileState::READABLE,
            new_state,
            FileSignals::empty(),
            cb_queue,
        );
    }

    fn update_state(
        &mut self,
        mask: FileState,
        state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let old_state = self.state;

        // Remove the mask, then copy the masked flags.
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, signals, cb_queue);
    }

    fn handle_state_change(
        &mut self,
        old_state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let states_changed = self.state ^ old_state;

        // Just return if nothing changed.
        if states_changed.is_empty() && signals.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, signals, cb_queue);
    }
}
