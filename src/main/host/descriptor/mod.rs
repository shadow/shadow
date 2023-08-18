use std::cmp::Ordering;
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use linux_api::fcntl::{DescriptorFlags, OFlag};
use linux_api::ioctls::IoctlRequest;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use socket::{Socket, SocketRef, SocketRefMut};

use super::host::Host;
use crate::core::worker;
use crate::cshadow as c;
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::IoVec;
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::callback_queue::{CallbackQueue, EventSource, Handle};
use crate::utility::{HostTreePointer, IsSend, IsSync, ObjectCounter};

pub mod descriptor_table;
pub mod epoll;
pub mod eventfd;
pub mod pipe;
pub mod shared_buf;
pub mod socket;
pub mod timerfd;

bitflags::bitflags! {
    /// These are flags that can potentially be changed from the plugin (analagous to the Linux
    /// `filp->f_flags` status flags). Not all `O_` flags are valid here. For example file access
    /// mode flags (ex: `O_RDWR`) are stored elsewhere, and file creation flags (ex: `O_CREAT`)
    /// are not stored anywhere. Many of these can be represented in different ways, for example:
    /// `O_NONBLOCK`, `SOCK_NONBLOCK`, `EFD_NONBLOCK`, `GRND_NONBLOCK`, etc, and not all have the
    /// same value.
    #[derive(Copy, Clone, Debug)]
    pub struct FileStatus: i32 {
        const NONBLOCK = OFlag::O_NONBLOCK.bits();
        const APPEND = OFlag::O_APPEND.bits();
        const ASYNC = OFlag::O_ASYNC.bits();
        const DIRECT = OFlag::O_DIRECT.bits();
        const NOATIME = OFlag::O_NOATIME.bits();
    }
}

impl FileStatus {
    pub fn as_o_flags(&self) -> OFlag {
        OFlag::from_bits(self.bits()).unwrap()
    }

    /// Returns a tuple of the `FileStatus` and any remaining flags.
    pub fn from_o_flags(flags: OFlag) -> (Self, OFlag) {
        let status = Self::from_bits_truncate(flags.bits());
        let remaining = flags.bits() & !status.bits();
        (status, OFlag::from_bits(remaining).unwrap())
    }
}

bitflags::bitflags! {
    /// These are flags that should generally not change (analagous to the Linux `filp->f_mode`).
    /// Since the plugin will never see these values and they're not exposed by the kernel, we
    /// don't match the kernel `FMODE_` values here.
    ///
    /// Examples: https://github.com/torvalds/linux/blob/master/include/linux/fs.h#L111
    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    pub struct FileMode: u32 {
        const READ = 0b00000001;
        const WRITE = 0b00000010;
    }
}

impl FileMode {
    pub fn as_o_flags(&self) -> OFlag {
        const READ_AND_WRITE: FileMode = FileMode::READ.union(FileMode::WRITE);
        const EMPTY: FileMode = FileMode::empty();

        // https://www.gnu.org/software/libc/manual/html_node/Access-Modes.html
        match *self {
            READ_AND_WRITE => OFlag::O_RDWR,
            Self::READ => OFlag::O_RDONLY,
            Self::WRITE => OFlag::O_WRONLY,
            // a linux-specific flag
            EMPTY => OFlag::O_PATH,
            _ => panic!("Invalid file mode flags"),
        }
    }

    /// Returns a tuple of the `FileMode` and any remaining flags, or an empty `Err` if
    /// the flags aren't valid (for example specifying both `O_RDWR` and `O_WRONLY`).
    #[allow(clippy::result_unit_err)]
    pub fn from_o_flags(flags: OFlag) -> Result<(Self, OFlag), ()> {
        // apply the access mode mask (the O_PATH flag is not contained within the access
        // mode mask, so we need to add it separately)
        let mode = flags & (OFlag::O_ACCMODE | OFlag::O_PATH);
        let remaining = flags - (OFlag::O_ACCMODE | OFlag::O_PATH);

        // https://www.gnu.org/software/libc/manual/html_node/Access-Modes.html
        let mode = match mode {
            OFlag::O_RDONLY => FileMode::READ,
            OFlag::O_WRONLY => FileMode::WRITE,
            OFlag::O_RDWR => FileMode::READ | FileMode::WRITE,
            OFlag::O_PATH => FileMode::empty(),
            _ => return Err(()),
        };

        Ok((mode, remaining))
    }
}

bitflags::bitflags! {
    #[derive(Default, Copy, Clone, Debug)]
    pub struct FileState: libc::c_int {
        /// Has been initialized and it is now OK to unblock any plugin waiting on a particular
        /// state. (This is a legacy C state and should be considered deprecated.)
        const ACTIVE = c::_Status_STATUS_FILE_ACTIVE;
        /// Can be read, i.e. there is data waiting for user.
        const READABLE = c::_Status_STATUS_FILE_READABLE;
        /// Can be written, i.e. there is available buffer space.
        const WRITABLE = c::_Status_STATUS_FILE_WRITABLE;
        /// User already called close.
        const CLOSED = c::_Status_STATUS_FILE_CLOSED;
        /// A wakeup operation occurred on a futex.
        const FUTEX_WAKEUP = c::_Status_STATUS_FUTEX_WAKEUP;
        /// A child process had an event reportable via e.g. waitpid.
        const CHILD_EVENT = c::_Status_STATUS_CHILD_EVENT;
        /// A listening socket is allowing connections. Only applicable to connection-oriented unix
        /// sockets.
        const SOCKET_ALLOWING_CONNECT = c::_Status_STATUS_SOCKET_ALLOWING_CONNECT;
    }
}

impl From<c::Status> for FileState {
    fn from(status: c::Status) -> Self {
        // if any unexpected bits were present, then it's an error
        Self::from_bits(status).unwrap()
    }
}

impl From<FileState> for c::Status {
    fn from(state: FileState) -> Self {
        state.bits()
    }
}

#[derive(Clone, Copy, Debug)]
pub enum StateListenerFilter {
    Never,
    OffToOn,
    OnToOff,
    Always,
}

/// A wrapper for a `*mut c::StatusListener` that increments its ref count when created,
/// and decrements when dropped.
struct LegacyListener(HostTreePointer<c::StatusListener>);

impl LegacyListener {
    fn new(ptr: HostTreePointer<c::StatusListener>) -> Self {
        assert!(!unsafe { ptr.ptr().is_null() });
        unsafe { c::statuslistener_ref(ptr.ptr()) };
        Self(ptr)
    }
}

impl std::ops::Deref for LegacyListener {
    type Target = HostTreePointer<c::StatusListener>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Drop for LegacyListener {
    fn drop(&mut self) {
        unsafe { c::statuslistener_unref(self.0.ptr()) };
    }
}

/// Stores event listener handles so that `c::StatusListener` objects can subscribe to events.
struct LegacyListenerHelper {
    // We expect only a small number of listeners at a time, which means that performance is
    // generally better and memory usage is lower with a `Vec` than a `HashMap`. The `usize` is the
    // pointer of the [`c::StatusListener`] that corresponds to this [`Handle`].
    handles: Vec<(usize, Handle<(FileState, FileState)>)>,
}

impl LegacyListenerHelper {
    fn new() -> Self {
        Self {
            handles: Vec::new(),
        }
    }

    fn add_listener(
        &mut self,
        ptr: HostTreePointer<c::StatusListener>,
        event_source: &mut EventSource<(FileState, FileState)>,
    ) {
        assert!(!unsafe { ptr.ptr() }.is_null());

        // if it's already listening, don't add a second time
        if self
            .handles
            .iter()
            .any(|x| x.0 == (unsafe { ptr.ptr() } as usize))
        {
            return;
        }

        // this will ref the pointer and unref it when the closure is dropped
        let ptr_wrapper = LegacyListener::new(ptr);

        let handle = event_source.add_listener(move |(state, changed), _cb_queue| unsafe {
            c::statuslistener_onStatusChanged(ptr_wrapper.ptr(), state.into(), changed.into())
        });

        // use a usize as the key so we don't accidentally deref the pointer
        self.handles.push((unsafe { ptr.ptr() } as usize, handle));
    }

    fn remove_listener(&mut self, ptr: *mut c::StatusListener) {
        assert!(!ptr.is_null());

        // find the position and remove it
        if let Some(x) = self.handles.iter().position(|x| x.0 == (ptr as usize)) {
            // drop the handle
            let _ = self.handles.remove(x);
        }
    }
}

/// A specified event source that passes a state and the changed bits to the function, but only if
/// the monitored bits have changed and if the change the filter is satisfied.
pub struct StateEventSource {
    inner: EventSource<(FileState, FileState)>,
    legacy_helper: LegacyListenerHelper,
}

impl StateEventSource {
    pub fn new() -> Self {
        Self {
            inner: EventSource::new(),
            legacy_helper: LegacyListenerHelper::new(),
        }
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.inner.add_listener(move |(state, changed), cb_queue| {
            // true if any of the bits we're monitoring have changed
            let flipped = monitoring.intersects(changed);

            // true if any of the bits we're monitoring are set
            let on = monitoring.intersects(state);

            let notify = match filter {
                // at least one monitored bit is on, and at least one has changed
                StateListenerFilter::OffToOn => flipped && on,
                // all monitored bits are off, and at least one has changed
                StateListenerFilter::OnToOff => flipped && !on,
                // at least one monitored bit has changed
                StateListenerFilter::Always => flipped,
                StateListenerFilter::Never => false,
            };

            if !notify {
                return;
            }

            (notify_fn)(state, changed, cb_queue)
        })
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        self.legacy_helper.add_listener(ptr, &mut self.inner);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.legacy_helper.remove_listener(ptr);
    }

    pub fn notify_listeners(
        &mut self,
        state: FileState,
        changed: FileState,
        cb_queue: &mut CallbackQueue,
    ) {
        self.inner.notify_listeners((state, changed), cb_queue)
    }
}

impl Default for StateEventSource {
    fn default() -> Self {
        Self::new()
    }
}

/// A wrapper for any type of file object.
#[derive(Clone)]
pub enum File {
    Pipe(Arc<AtomicRefCell<pipe::Pipe>>),
    EventFd(Arc<AtomicRefCell<eventfd::EventFd>>),
    Socket(Socket),
    TimerFd(Arc<AtomicRefCell<timerfd::TimerFd>>),
    Epoll(Arc<AtomicRefCell<epoll::Epoll>>),
}

// will not compile if `File` is not Send + Sync
impl IsSend for File {}
impl IsSync for File {}

impl File {
    pub fn borrow(&self) -> FileRef {
        match self {
            Self::Pipe(ref f) => FileRef::Pipe(f.borrow()),
            Self::EventFd(ref f) => FileRef::EventFd(f.borrow()),
            Self::Socket(ref f) => FileRef::Socket(f.borrow()),
            Self::TimerFd(ref f) => FileRef::TimerFd(f.borrow()),
            Self::Epoll(ref f) => FileRef::Epoll(f.borrow()),
        }
    }

    pub fn try_borrow(&self) -> Result<FileRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::Pipe(ref f) => FileRef::Pipe(f.try_borrow()?),
            Self::EventFd(ref f) => FileRef::EventFd(f.try_borrow()?),
            Self::Socket(ref f) => FileRef::Socket(f.try_borrow()?),
            Self::TimerFd(ref f) => FileRef::TimerFd(f.try_borrow()?),
            Self::Epoll(ref f) => FileRef::Epoll(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> FileRefMut {
        match self {
            Self::Pipe(ref f) => FileRefMut::Pipe(f.borrow_mut()),
            Self::EventFd(ref f) => FileRefMut::EventFd(f.borrow_mut()),
            Self::Socket(ref f) => FileRefMut::Socket(f.borrow_mut()),
            Self::TimerFd(ref f) => FileRefMut::TimerFd(f.borrow_mut()),
            Self::Epoll(ref f) => FileRefMut::Epoll(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<FileRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::Pipe(ref f) => FileRefMut::Pipe(f.try_borrow_mut()?),
            Self::EventFd(ref f) => FileRefMut::EventFd(f.try_borrow_mut()?),
            Self::Socket(ref f) => FileRefMut::Socket(f.try_borrow_mut()?),
            Self::TimerFd(ref f) => FileRefMut::TimerFd(f.try_borrow_mut()?),
            Self::Epoll(ref f) => FileRefMut::Epoll(f.try_borrow_mut()?),
        })
    }

    pub fn canonical_handle(&self) -> usize {
        match self {
            Self::Pipe(f) => Arc::as_ptr(f) as usize,
            Self::EventFd(f) => Arc::as_ptr(f) as usize,
            Self::Socket(ref f) => f.canonical_handle(),
            Self::TimerFd(f) => Arc::as_ptr(f) as usize,
            Self::Epoll(f) => Arc::as_ptr(f) as usize,
        }
    }
}

impl Eq for File {}

impl PartialEq for File {
    fn eq(&self, other: &Self) -> bool {
        self.canonical_handle() == other.canonical_handle()
    }
}

impl Ord for File {
    fn cmp(&self, other: &Self) -> Ordering {
        self.canonical_handle().cmp(&other.canonical_handle())
    }
}

impl PartialOrd for File {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl std::fmt::Debug for File {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pipe(_) => write!(f, "Pipe")?,
            Self::EventFd(_) => write!(f, "EventFd")?,
            Self::Socket(_) => write!(f, "Socket")?,
            Self::TimerFd(_) => write!(f, "TimerFd")?,
            Self::Epoll(_) => write!(f, "Epoll")?,
        }

        if let Ok(file) = self.try_borrow() {
            write!(
                f,
                "(state: {:?}, status: {:?})",
                file.state(),
                file.get_status()
            )
        } else {
            write!(f, "(already borrowed)")
        }
    }
}

pub enum FileRef<'a> {
    Pipe(atomic_refcell::AtomicRef<'a, pipe::Pipe>),
    EventFd(atomic_refcell::AtomicRef<'a, eventfd::EventFd>),
    Socket(SocketRef<'a>),
    TimerFd(atomic_refcell::AtomicRef<'a, timerfd::TimerFd>),
    Epoll(atomic_refcell::AtomicRef<'a, epoll::Epoll>),
}

pub enum FileRefMut<'a> {
    Pipe(atomic_refcell::AtomicRefMut<'a, pipe::Pipe>),
    EventFd(atomic_refcell::AtomicRefMut<'a, eventfd::EventFd>),
    Socket(SocketRefMut<'a>),
    TimerFd(atomic_refcell::AtomicRefMut<'a, timerfd::TimerFd>),
    Epoll(atomic_refcell::AtomicRefMut<'a, epoll::Epoll>),
}

impl FileRef<'_> {
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn supports_sa_restart(&self) -> bool
    );
}

impl FileRefMut<'_> {
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn supports_sa_restart(&self) -> bool
    );
    enum_passthrough!(self, (val), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn set_has_open_file(&mut self, val: bool)
    );
    enum_passthrough!(self, (cb_queue), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
    enum_passthrough!(self, (status), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn ioctl(&mut self, request: IoctlRequest, arg_ptr: ForeignPtr<()>, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (monitoring, filter, notify_fn), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn add_listener(
            &mut self,
            monitoring: FileState,
            filter: StateListenerFilter,
            notify_fn: impl Fn(FileState, FileState, &mut CallbackQueue) + Send + Sync + 'static,
        ) -> Handle<(FileState, FileState)>
    );
    enum_passthrough!(self, (ptr), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>)
    );
    enum_passthrough!(self, (ptr), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );
    enum_passthrough!(self, (iovs, offset, flags, mem, cb_queue), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn readv(&mut self, iovs: &[IoVec], offset: Option<libc::off_t>, flags: libc::c_int,
                     mem: &mut MemoryManager, cb_queue: &mut CallbackQueue) -> Result<libc::ssize_t, SyscallError>
    );
    enum_passthrough!(self, (iovs, offset, flags, mem, cb_queue), Pipe, EventFd, Socket, TimerFd, Epoll;
        pub fn writev(&mut self, iovs: &[IoVec], offset: Option<libc::off_t>, flags: libc::c_int,
                      mem: &mut MemoryManager, cb_queue: &mut CallbackQueue) -> Result<libc::ssize_t, SyscallError>
    );
}

impl std::fmt::Debug for FileRef<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pipe(_) => write!(f, "Pipe")?,
            Self::EventFd(_) => write!(f, "EventFd")?,
            Self::Socket(_) => write!(f, "Socket")?,
            Self::TimerFd(_) => write!(f, "TimerFd")?,
            Self::Epoll(_) => write!(f, "Epoll")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.get_status()
        )
    }
}

impl std::fmt::Debug for FileRefMut<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pipe(_) => write!(f, "Pipe")?,
            Self::EventFd(_) => write!(f, "EventFd")?,
            Self::Socket(_) => write!(f, "Socket")?,
            Self::TimerFd(_) => write!(f, "TimerFd")?,
            Self::Epoll(_) => write!(f, "Epoll")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.get_status()
        )
    }
}

/// Represents a POSIX file description, or a Linux `struct file`. An `OpenFile` wraps a reference
/// to a `File`. Once there are no more `OpenFile` objects for a given `File`, the `File` will be
/// closed. Typically this means that holding an `OpenFile` will ensure that the file remains open
/// (the file's status will not become `FileStatus::CLOSED`), but the underlying file may close
/// itself in extenuating circumstances (for example if the file has an internal error).
///
/// **Safety:** If an `OpenFile` for a specific file already exists, it is an error to create a new
/// `OpenFile` for that file. You must clone the existing `OpenFile` object. A new `OpenFile` object
/// should probably only ever be created for a newly created file object. Otherwise for existing
/// file objects, it won't be clear if there are already-existing `OpenFile` objects for that file.
///
/// There must also not be any existing mutable borrows of the file when an `OpenFile` is created.
#[derive(Clone, Debug)]
pub struct OpenFile {
    inner: Arc<OpenFileInner>,
    _counter: ObjectCounter,
}

// will not compile if `OpenFile` is not Send + Sync
impl IsSend for OpenFile {}
impl IsSync for OpenFile {}

impl OpenFile {
    pub fn new(file: File) -> Self {
        {
            let mut file = file.borrow_mut();

            if file.state().contains(FileState::CLOSED) {
                // panic if debug assertions are enabled
                debug_panic!("Creating an `OpenFile` object for a closed file");
            }

            if file.has_open_file() {
                // panic if debug assertions are enabled
                debug_panic!(
                    "Creating an `OpenFile` object for a file that already has an `OpenFile` object"
                );
            }

            file.set_has_open_file(true);
        }

        Self {
            inner: Arc::new(OpenFileInner::new(file)),
            _counter: ObjectCounter::new("OpenFile"),
        }
    }

    pub fn inner_file(&self) -> &File {
        self.inner.file.as_ref().unwrap()
    }

    /// Will close the inner `File` object if this is the last `OpenFile` for that `File`. This
    /// behaviour is the same as simply dropping this `OpenFile` object, but allows you to pass an
    /// event queue and get the return value of the close operation.
    pub fn close(self, cb_queue: &mut CallbackQueue) -> Option<Result<(), SyscallError>> {
        let OpenFile { inner, _counter } = self;

        // if this is the last reference, call close() on the file
        Arc::into_inner(inner).map(|inner| inner.close(cb_queue))
    }
}

#[derive(Clone, Debug)]
struct OpenFileInner {
    file: Option<File>,
    _counter: ObjectCounter,
}

impl OpenFileInner {
    pub fn new(file: File) -> Self {
        Self {
            file: Some(file),
            _counter: ObjectCounter::new("OpenFileInner"),
        }
    }

    pub fn close(mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        self.close_helper(cb_queue)
    }

    fn close_helper(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        if let Some(file) = self.file.take() {
            file.borrow_mut().close(cb_queue)?;
        }
        Ok(())
    }
}

impl std::ops::Drop for OpenFileInner {
    fn drop(&mut self) {
        // ignore any return value
        let _ = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| self.close_helper(cb_queue))
        });
    }
}

/// A file descriptor that reference an open file. Also contains flags that change the behaviour of
/// this file descriptor.
#[derive(Debug, Clone)]
pub struct Descriptor {
    /// The file that this descriptor points to.
    file: CompatFile,
    /// Descriptor flags.
    flags: DescriptorFlags,
    _counter: ObjectCounter,
}

// will not compile if `Descriptor` is not Send + Sync
impl IsSend for Descriptor {}
impl IsSync for Descriptor {}

impl Descriptor {
    pub fn new(file: CompatFile) -> Self {
        Self {
            file,
            flags: DescriptorFlags::empty(),
            _counter: ObjectCounter::new("Descriptor"),
        }
    }

    pub fn file(&self) -> &CompatFile {
        &self.file
    }

    pub fn flags(&self) -> DescriptorFlags {
        self.flags
    }

    pub fn set_flags(&mut self, flags: DescriptorFlags) {
        self.flags = flags;
    }

    pub fn into_file(self) -> CompatFile {
        self.file
    }

    /// Close the descriptor. The `host` option is a legacy option for legacy file.
    pub fn close(
        self,
        host: &Host,
        cb_queue: &mut CallbackQueue,
    ) -> Option<Result<(), SyscallError>> {
        self.file.close(host, cb_queue)
    }

    /// Duplicate the descriptor, with both descriptors pointing to the same `OpenFile`. In
    /// Linux, the descriptor flags aren't typically copied to the new descriptor, so we
    /// explicitly require a flags value to avoid confusion.
    pub fn dup(&self, flags: DescriptorFlags) -> Self {
        Self {
            file: self.file.clone(),
            flags,
            _counter: ObjectCounter::new("Descriptor"),
        }
    }

    pub fn into_raw(descriptor: Box<Self>) -> *mut Self {
        Box::into_raw(descriptor)
    }

    pub fn from_raw(descriptor: *mut Self) -> Option<Box<Self>> {
        if descriptor.is_null() {
            return None;
        }

        unsafe { Some(Box::from_raw(descriptor)) }
    }

    /// The new descriptor takes ownership of the reference to the legacy file and does not
    /// increment its ref count, but will decrement the ref count when this descriptor is
    /// freed/dropped with `descriptor_free()`. The descriptor flags must be either 0 or
    /// `O_CLOEXEC`.
    ///
    /// If creating a descriptor for a `TCP` object, you should use `descriptor_fromLegacyTcp`
    /// instead. If `legacy_file` is a TCP socket, this function will panic.
    ///
    /// # Safety
    ///
    /// Takes ownership of `legacy_file`, which must be safely dereferenceable.
    pub unsafe fn from_legacy_file(
        legacy_file: *mut c::LegacyFile,
        descriptor_flags: OFlag,
    ) -> Descriptor {
        assert!(!legacy_file.is_null());

        // if it's a TCP socket, `descriptor_fromLegacyTcp` should be used instead
        assert_ne!(
            unsafe { c::legacyfile_getType(legacy_file) },
            c::_LegacyFileType_DT_TCPSOCKET,
        );

        let mut descriptor = Descriptor::new(CompatFile::Legacy(LegacyFileCounter::new(
            CountedLegacyFileRef::new(HostTreePointer::new(legacy_file)),
        )));

        let (descriptor_flags, remaining) = DescriptorFlags::from_o_flags(descriptor_flags);
        assert!(remaining.is_empty());
        descriptor.set_flags(descriptor_flags);
        descriptor
    }
}

/// Represents a counted reference to a legacy file object. Will decrement the legacy file's ref
/// count when dropped.
#[derive(Debug)]
pub struct CountedLegacyFileRef(HostTreePointer<c::LegacyFile>);

impl CountedLegacyFileRef {
    /// Does not increment the legacy file's ref count, but will decrement the ref count when
    /// dropped.
    pub fn new(ptr: HostTreePointer<c::LegacyFile>) -> Self {
        Self(ptr)
    }

    /// # Safety
    ///
    /// See `HostTreePointer::ptr`.
    pub unsafe fn ptr(&self) -> *mut c::LegacyFile {
        unsafe { self.0.ptr() }
    }
}

impl std::clone::Clone for CountedLegacyFileRef {
    fn clone(&self) -> Self {
        // ref the legacy file object
        unsafe { c::legacyfile_ref(self.0.ptr() as *mut core::ffi::c_void) };
        Self(self.0)
    }
}

impl Drop for CountedLegacyFileRef {
    fn drop(&mut self) {
        // unref the legacy file object
        unsafe { c::legacyfile_unref(self.0.ptr() as *mut core::ffi::c_void) };
    }
}

/// Used to track how many descriptors are open for a `LegacyFile`. When the `close()` method is
/// called, the legacy file's `legacyfile_close()` will only be called if this is the last
/// descriptor for that legacy file. This is similar to an `OpenFile` object.
#[derive(Clone, Debug)]
pub struct LegacyFileCounter {
    file: Option<CountedLegacyFileRef>,
    /// A count of how many open descriptors there are with reference to this legacy file.
    open_count: Arc<()>,
}

impl LegacyFileCounter {
    pub fn new(file: CountedLegacyFileRef) -> Self {
        Self {
            file: Some(file),
            open_count: Arc::new(()),
        }
    }

    pub fn ptr(&self) -> *mut c::LegacyFile {
        unsafe { self.file.as_ref().unwrap().ptr() }
    }

    /// Should drop `self` immediately after calling this.
    fn close_helper(&mut self, host: &Host) {
        // this isn't subject to race conditions since we should never access descriptors
        // from multiple threads at the same time
        if Arc::<()>::strong_count(&self.open_count) == 1 {
            if let Some(file) = self.file.take() {
                unsafe { c::legacyfile_close(file.ptr(), host) }
            }
        }
    }

    /// Close the descriptor, and if this is the last descriptor pointing to its legacy file, close
    /// the legacy file as well.
    pub fn close(mut self, host: &Host) {
        self.close_helper(host);
    }
}

impl std::ops::Drop for LegacyFileCounter {
    fn drop(&mut self) {
        worker::Worker::with_active_host(|host| self.close_helper(host)).unwrap();
    }
}

#[derive(Clone, Debug)]
pub enum CompatFile {
    New(OpenFile),
    Legacy(LegacyFileCounter),
}

impl CompatFile {
    /// Close the file. The `host` option is a legacy option for legacy files.
    pub fn close(
        self,
        host: &Host,
        cb_queue: &mut CallbackQueue,
    ) -> Option<Result<(), SyscallError>> {
        match self {
            Self::New(file) => file.close(cb_queue),
            Self::Legacy(file) => {
                file.close(host);
                Some(Ok(()))
            }
        }
    }
}

mod export {
    use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;

    use super::*;
    use crate::host::descriptor::socket::inet::legacy_tcp::LegacyTcpSocket;
    use crate::host::descriptor::socket::inet::InetSocket;
    use crate::utility::legacy_callback_queue::RootedRefCell_StateEventSource;

    /// The new descriptor takes ownership of the reference to the legacy file and does not
    /// increment its ref count, but will decrement the ref count when this descriptor is
    /// freed/dropped with `descriptor_free()`. The descriptor flags must be either 0 or
    /// `O_CLOEXEC`.
    ///
    /// If creating a descriptor for a `TCP` object, you should use `descriptor_fromLegacyTcp`
    /// instead. If `legacy_file` is a TCP socket, this function will panic.
    #[no_mangle]
    pub unsafe extern "C" fn descriptor_fromLegacyFile(
        legacy_file: *mut c::LegacyFile,
        descriptor_flags: libc::c_int,
    ) -> *mut Descriptor {
        let descriptor_flags = OFlag::from_bits(descriptor_flags).unwrap();
        let descriptor = unsafe { Descriptor::from_legacy_file(legacy_file, descriptor_flags) };
        Descriptor::into_raw(Box::new(descriptor))
    }

    /// The new descriptor takes ownership of the reference to the legacy TCP object and does not
    /// increment its ref count, but will decrement the ref count when this descriptor is
    /// freed/dropped with `descriptor_free()`. The descriptor flags must be either 0 or
    /// `O_CLOEXEC`.
    #[no_mangle]
    pub unsafe extern "C" fn descriptor_fromLegacyTcp(
        legacy_tcp: *mut c::TCP,
        descriptor_flags: libc::c_int,
    ) -> *mut Descriptor {
        assert!(!legacy_tcp.is_null());

        let tcp = unsafe { LegacyTcpSocket::new_from_legacy(legacy_tcp) };
        let mut descriptor = Descriptor::new(CompatFile::New(OpenFile::new(File::Socket(
            Socket::Inet(InetSocket::LegacyTcp(tcp)),
        ))));

        let descriptor_flags = OFlag::from_bits(descriptor_flags).unwrap();
        let (descriptor_flags, remaining) = DescriptorFlags::from_o_flags(descriptor_flags);
        assert!(remaining.is_empty());
        descriptor.set_flags(descriptor_flags);

        Descriptor::into_raw(Box::new(descriptor))
    }

    /// If the descriptor is a legacy file, returns a pointer to the legacy file object. Otherwise
    /// returns NULL. The legacy file's ref count is not modified, so the pointer must not outlive
    /// the lifetime of the descriptor.
    #[no_mangle]
    pub extern "C" fn descriptor_asLegacyFile(descriptor: *const Descriptor) -> *mut c::LegacyFile {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        if let CompatFile::Legacy(d) = descriptor.file() {
            d.ptr()
        } else {
            std::ptr::null_mut()
        }
    }

    /// If the descriptor is a new/rust descriptor, returns a pointer to the reference-counted
    /// `OpenFile` object. Otherwise returns NULL. The `OpenFile` object's ref count is not
    /// modified, so the returned pointer must not outlive the lifetime of the descriptor.
    #[no_mangle]
    pub extern "C" fn descriptor_borrowOpenFile(descriptor: *const Descriptor) -> *const OpenFile {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        match descriptor.file() {
            CompatFile::Legacy(_) => std::ptr::null_mut(),
            CompatFile::New(d) => d,
        }
    }

    /// If the descriptor is a new/rust descriptor, returns a pointer to the reference-counted
    /// `OpenFile` object. Otherwise returns NULL. The `OpenFile` object's ref count is incremented,
    /// so the returned pointer must always later be passed to `openfile_drop()`, otherwise the
    /// memory will leak.
    #[no_mangle]
    pub extern "C" fn descriptor_newRefOpenFile(descriptor: *const Descriptor) -> *const OpenFile {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        match descriptor.file() {
            CompatFile::Legacy(_) => std::ptr::null_mut(),
            CompatFile::New(d) => Box::into_raw(Box::new(d.clone())),
        }
    }

    /// The descriptor flags must be either 0 or `O_CLOEXEC`.
    #[no_mangle]
    pub extern "C" fn descriptor_setFlags(descriptor: *mut Descriptor, flags: libc::c_int) {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &mut *descriptor };
        let flags = OFlag::from_bits(flags).unwrap();
        let (flags, remaining_flags) = DescriptorFlags::from_o_flags(flags);
        assert!(remaining_flags.is_empty());

        descriptor.set_flags(flags);
    }

    /// Decrement the ref count of the `OpenFile` object. The pointer must not be used after calling
    /// this function.
    #[no_mangle]
    pub extern "C" fn openfile_drop(file: *const OpenFile) {
        assert!(!file.is_null());

        drop(unsafe { Box::from_raw(file as *mut OpenFile) });
    }

    /// Get the state of the `OpenFile` object.
    #[no_mangle]
    pub extern "C" fn openfile_getStatus(file: *const OpenFile) -> c::Status {
        assert!(!file.is_null());

        let file = unsafe { &*file };

        file.inner_file().borrow().state().into()
    }

    /// Add a status listener to the `OpenFile` object. This will increment the status listener's
    /// ref count, and will decrement the ref count when this status listener is removed or when the
    /// `OpenFile` is freed/dropped.
    #[no_mangle]
    pub unsafe extern "C" fn openfile_addListener(
        file: *const OpenFile,
        listener: *mut c::StatusListener,
    ) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = unsafe { &*file };

        file.inner_file()
            .borrow_mut()
            .add_legacy_listener(HostTreePointer::new(listener));
    }

    /// Remove a listener from the `OpenFile` object.
    #[no_mangle]
    pub extern "C" fn openfile_removeListener(
        file: *const OpenFile,
        listener: *mut c::StatusListener,
    ) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = unsafe { &*file };

        file.inner_file()
            .borrow_mut()
            .remove_legacy_listener(listener);
    }

    /// Get the canonical handle for an `OpenFile` object. Two `OpenFile` objects refer to the same
    /// underlying data if their handles are equal.
    #[no_mangle]
    pub extern "C" fn openfile_getCanonicalHandle(file: *const OpenFile) -> libc::uintptr_t {
        assert!(!file.is_null());

        let file = unsafe { &*file };

        file.inner_file().canonical_handle()
    }

    /// If the descriptor is a new/rust descriptor, returns a pointer to the reference-counted
    /// `File` object. Otherwise returns NULL. The `File` object's ref count is incremented, so the
    /// pointer must always later be passed to `file_drop()`, otherwise the memory will leak.
    #[no_mangle]
    pub extern "C" fn descriptor_newRefFile(descriptor: *const Descriptor) -> *const File {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        match descriptor.file() {
            CompatFile::Legacy(_) => std::ptr::null_mut(),
            CompatFile::New(d) => Box::into_raw(Box::new(d.inner_file().clone())),
        }
    }

    /// Decrement the ref count of the `File` object. The pointer must not be used after calling
    /// this function.
    #[no_mangle]
    pub extern "C" fn file_drop(file: *const File) {
        assert!(!file.is_null());

        drop(unsafe { Box::from_raw(file as *mut File) });
    }

    /// Increment the ref count of the `File` object. The returned pointer will not be the same as
    /// the given pointer (they are distinct references), and they both must be dropped with
    /// `file_drop` separately later.
    #[no_mangle]
    pub extern "C" fn file_cloneRef(file: *const File) -> *const File {
        let file = unsafe { file.as_ref() }.unwrap();
        Box::into_raw(Box::new(file.clone()))
    }

    /// Get the state of the `File` object.
    #[no_mangle]
    pub extern "C" fn file_getStatus(file: *const File) -> c::Status {
        assert!(!file.is_null());

        let file = unsafe { &*file };

        file.borrow().state().into()
    }

    /// Add a status listener to the `File` object. This will increment the status listener's ref
    /// count, and will decrement the ref count when this status listener is removed or when the
    /// `File` is freed/dropped.
    #[no_mangle]
    pub unsafe extern "C" fn file_addListener(file: *const File, listener: *mut c::StatusListener) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = unsafe { &*file };

        file.borrow_mut()
            .add_legacy_listener(HostTreePointer::new(listener));
    }

    /// Remove a listener from the `File` object.
    #[no_mangle]
    pub extern "C" fn file_removeListener(file: *const File, listener: *mut c::StatusListener) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = unsafe { &*file };

        file.borrow_mut().remove_legacy_listener(listener);
    }

    /// Get the canonical handle for a `File` object. Two `File` objects refer to the same
    /// underlying data if their handles are equal.
    #[no_mangle]
    pub extern "C" fn file_getCanonicalHandle(file: *const File) -> libc::uintptr_t {
        assert!(!file.is_null());

        let file = unsafe { &*file };

        file.canonical_handle()
    }

    #[no_mangle]
    pub extern "C" fn eventsource_new() -> *mut RootedRefCell_StateEventSource {
        let event_source = worker::Worker::with_active_host(|host| {
            Box::new(RootedRefCell::new(host.root(), StateEventSource::new()))
        })
        .unwrap();
        Box::into_raw(event_source)
    }

    #[no_mangle]
    pub extern "C" fn eventsource_free(event_source: *mut RootedRefCell_StateEventSource) {
        assert!(!event_source.is_null());
        drop(unsafe { Box::from_raw(event_source) });
    }

    #[no_mangle]
    pub extern "C" fn eventsource_addLegacyListener(
        event_source: *const RootedRefCell_StateEventSource,
        listener: *mut c::StatusListener,
    ) {
        let event_source = unsafe { event_source.as_ref() }.unwrap();
        worker::Worker::with_active_host(|host| {
            let mut event_source = event_source.borrow_mut(host.root());

            event_source.add_legacy_listener(HostTreePointer::new(listener));
        })
        .unwrap();
    }

    #[no_mangle]
    pub extern "C" fn eventsource_removeLegacyListener(
        event_source: *const RootedRefCell_StateEventSource,
        listener: *mut c::StatusListener,
    ) {
        let event_source = unsafe { event_source.as_ref() }.unwrap();
        worker::Worker::with_active_host(|host| {
            let mut event_source = event_source.borrow_mut(host.root());

            event_source.remove_legacy_listener(listener);
        })
        .unwrap();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host::syscall::Trigger;
    use crate::host::syscall_condition::SysCallCondition;
    use crate::host::syscall_types::{
        Blocked, Failed, SyscallError, SyscallReturn, SyscallReturnBlocked, SyscallReturnDone,
    };

    #[test]
    // can't call foreign function: syscallcondition_new
    #[cfg_attr(miri, ignore)]
    fn test_syscallresult_roundtrip() {
        for val in vec![
            Ok(1.into()),
            Err(linux_api::errno::Errno::EPERM.into()),
            Err(SyscallError::Failed(Failed {
                errno: linux_api::errno::Errno::EINTR,
                restartable: true,
            })),
            Err(SyscallError::Failed(Failed {
                errno: linux_api::errno::Errno::EINTR,
                restartable: false,
            })),
            Err(SyscallError::Blocked(Blocked {
                condition: SysCallCondition::new(Trigger::from(c::Trigger {
                    type_: 1,
                    object: c::TriggerObject {
                        as_pointer: std::ptr::null_mut(),
                    },
                    status: 2,
                })),
                restartable: true,
            })),
        ]
        .drain(..)
        {
            // We can't easily compare the value to the roundtripped result, since
            // roundtripping consumes the original value, and SyscallReturn doesn't implement Clone.
            // Compare their debug strings instead.
            let orig_debug = format!("{:?}", &val);
            let roundtripped = SyscallResult::from(SyscallReturn::from(val));
            let roundtripped_debug = format!("{:?}", roundtripped);
            assert_eq!(orig_debug, roundtripped_debug);
        }
    }

    #[test]
    // can't call foreign function: syscallcondition_new
    #[cfg_attr(miri, ignore)]
    fn test_syscallreturn_roundtrip() {
        let condition = SysCallCondition::new(Trigger::from(c::Trigger {
            type_: 1,
            object: c::TriggerObject {
                as_pointer: std::ptr::null_mut(),
            },
            status: 2,
        }));
        for val in vec![
            SyscallReturn::Done(SyscallReturnDone {
                retval: 1.into(),
                restartable: false,
            }),
            SyscallReturn::Block(SyscallReturnBlocked {
                cond: condition.into_inner(),
                restartable: true,
            }),
            SyscallReturn::Native,
        ]
        .drain(..)
        {
            // We can't easily compare the value to the roundtripped result,
            // since roundtripping consumes the original value, and
            // SyscallReturn doesn't implement Clone. Compare their debug
            // strings instead.
            let orig_debug = format!("{:?}", &val);
            let roundtripped = SyscallReturn::from(SyscallResult::from(val));
            let roundtripped_debug = format!("{:?}", roundtripped);
            assert_eq!(orig_debug, roundtripped_debug);
        }
    }

    #[test]
    fn test_file_mode_o_flags() {
        // test from O flags to FileMode
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_PATH),
            Ok((FileMode::empty(), OFlag::empty()))
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_WRONLY),
            Ok((FileMode::WRITE, OFlag::empty()))
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_RDWR),
            Ok((FileMode::READ | FileMode::WRITE, OFlag::empty()))
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_RDONLY),
            Ok((FileMode::READ, OFlag::empty()))
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::empty()),
            Ok((FileMode::READ, OFlag::empty()))
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_RDWR | OFlag::O_WRONLY),
            Err(())
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_RDWR | OFlag::O_RDONLY),
            Ok((FileMode::READ | FileMode::WRITE, OFlag::empty()))
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_WRONLY | OFlag::O_RDONLY),
            Ok((FileMode::WRITE, OFlag::empty()))
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_PATH | OFlag::O_WRONLY),
            Err(())
        );
        assert_eq!(
            FileMode::from_o_flags(OFlag::O_WRONLY | OFlag::O_CLOEXEC),
            Ok((FileMode::WRITE, OFlag::O_CLOEXEC))
        );

        // test from FileMode to O flags
        assert_eq!(FileMode::as_o_flags(&FileMode::empty()), OFlag::O_PATH);
        assert_eq!(FileMode::as_o_flags(&FileMode::READ), OFlag::O_RDONLY);
        assert_eq!(FileMode::as_o_flags(&FileMode::WRITE), OFlag::O_WRONLY);
        assert_eq!(
            FileMode::as_o_flags(&(FileMode::READ | FileMode::WRITE)),
            OFlag::O_RDWR
        );
    }
}
