use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::fcntl::OFlag;

use crate::cshadow as c;
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SyscallError, SyscallResult};
use crate::utility::callback_queue::{CallbackQueue, EventSource, Handle};
use crate::utility::{HostTreePointer, IsSend, IsSync};

use socket::{Socket, SocketRef, SocketRefMut};

use super::host::Host;

pub mod descriptor_table;
pub mod eventfd;
pub mod pipe;
pub mod shared_buf;
pub mod socket;

bitflags::bitflags! {
    /// These are flags that can potentially be changed from the plugin (analagous to the Linux
    /// `filp->f_flags` status flags). Not all `O_` flags are valid here. For example file access
    /// mode flags (ex: `O_RDWR`) are stored elsewhere, and file creation flags (ex: `O_CREAT`)
    /// are not stored anywhere. Many of these can be represented in different ways, for example:
    /// `O_NONBLOCK`, `SOCK_NONBLOCK`, `EFD_NONBLOCK`, `GRND_NONBLOCK`, etc, and not all have the
    /// same value.
    pub struct FileStatus: libc::c_int {
        const NONBLOCK = libc::O_NONBLOCK;
        const APPEND = libc::O_APPEND;
        const ASYNC = libc::O_ASYNC;
        const DIRECT = libc::O_DIRECT;
        const NOATIME = libc::O_NOATIME;
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
    #[derive(Default)]
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

#[derive(Clone, Debug)]
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
    handle_map: std::collections::HashMap<usize, Handle<(FileState, FileState)>>,
}

impl LegacyListenerHelper {
    fn new() -> Self {
        Self {
            handle_map: std::collections::HashMap::new(),
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
            .handle_map
            .contains_key(&(unsafe { ptr.ptr() } as usize))
        {
            return;
        }

        // this will ref the pointer and unref it when the closure is dropped
        let ptr_wrapper = LegacyListener::new(ptr);

        let handle = event_source.add_listener(move |(state, changed), _cb_queue| unsafe {
            c::statuslistener_onStatusChanged(ptr_wrapper.ptr(), state.into(), changed.into())
        });

        // use a usize as the key so we don't accidentally deref the pointer
        self.handle_map
            .insert(unsafe { ptr.ptr() } as usize, handle);
    }

    fn remove_listener(&mut self, ptr: *mut c::StatusListener) {
        assert!(!ptr.is_null());
        self.handle_map.remove(&(ptr as usize));
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

/// A wrapper for any type of file object.
#[derive(Clone)]
pub enum File {
    Pipe(Arc<AtomicRefCell<pipe::Pipe>>),
    EventFd(Arc<AtomicRefCell<eventfd::EventFd>>),
    Socket(Socket),
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
        }
    }

    pub fn try_borrow(&self) -> Result<FileRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::Pipe(ref f) => FileRef::Pipe(f.try_borrow()?),
            Self::EventFd(ref f) => FileRef::EventFd(f.try_borrow()?),
            Self::Socket(ref f) => FileRef::Socket(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> FileRefMut {
        match self {
            Self::Pipe(ref f) => FileRefMut::Pipe(f.borrow_mut()),
            Self::EventFd(ref f) => FileRefMut::EventFd(f.borrow_mut()),
            Self::Socket(ref f) => FileRefMut::Socket(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<FileRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::Pipe(ref f) => FileRefMut::Pipe(f.try_borrow_mut()?),
            Self::EventFd(ref f) => FileRefMut::EventFd(f.try_borrow_mut()?),
            Self::Socket(ref f) => FileRefMut::Socket(f.try_borrow_mut()?),
        })
    }

    pub fn canonical_handle(&self) -> usize {
        match self {
            Self::Pipe(f) => Arc::as_ptr(f) as usize,
            Self::EventFd(f) => Arc::as_ptr(f) as usize,
            Self::Socket(ref f) => f.canonical_handle(),
        }
    }
}

impl std::fmt::Debug for File {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pipe(_) => write!(f, "Pipe")?,
            Self::EventFd(_) => write!(f, "EventFd")?,
            Self::Socket(_) => write!(f, "Socket")?,
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
}

pub enum FileRefMut<'a> {
    Pipe(atomic_refcell::AtomicRefMut<'a, pipe::Pipe>),
    EventFd(atomic_refcell::AtomicRefMut<'a, eventfd::EventFd>),
    Socket(SocketRefMut<'a>),
}

impl FileRef<'_> {
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn supports_sa_restart(&self) -> bool
    );
}

impl FileRefMut<'_> {
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn supports_sa_restart(&self) -> bool
    );
    enum_passthrough!(self, (val), Pipe, EventFd, Socket;
        pub fn set_has_open_file(&mut self, val: bool)
    );
    enum_passthrough!(self, (cb_queue), Pipe, EventFd, Socket;
        pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
    enum_passthrough!(self, (status), Pipe, EventFd, Socket;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), Pipe, EventFd, Socket;
        pub fn ioctl(&mut self, request: u64, arg_ptr: PluginPtr, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (ptr), Pipe, EventFd, Socket;
        pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>)
    );
    enum_passthrough!(self, (ptr), Pipe, EventFd, Socket;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );

    enum_passthrough_generic!(self, (bytes, offset, cb_queue), Pipe, EventFd, Socket;
        pub fn read<W>(&mut self, bytes: W, offset: libc::off_t, cb_queue: &mut CallbackQueue) -> SyscallResult
        where W: std::io::Write + std::io::Seek
    );

    enum_passthrough_generic!(self, (source, offset, cb_queue), Pipe, EventFd, Socket;
        pub fn write<R>(&mut self, source: R, offset: libc::off_t, cb_queue: &mut CallbackQueue) -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );
}

impl std::fmt::Debug for FileRef<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pipe(_) => write!(f, "Pipe")?,
            Self::EventFd(_) => write!(f, "EventFd")?,
            Self::Socket(_) => write!(f, "Socket")?,
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
            inner: Arc::new(OpenFileInner { file: Some(file) }),
        }
    }

    pub fn inner_file(&self) -> &File {
        &self.inner.file.as_ref().unwrap()
    }

    /// Will close the inner `File` object if this is the last `OpenFile` for that `File`. This
    /// behaviour is the same as simply dropping this `OpenFile` object, but allows you to pass an
    /// event queue and get the return value of the close operation.
    pub fn close(self, cb_queue: &mut CallbackQueue) -> Option<Result<(), SyscallError>> {
        let OpenFile { inner } = self;

        // note: There is a race-condition here in a multi-threaded context. Since shadow should
        // never be accessing host-specific objects from two threads at once, this shouldn't be an
        // issue, but documenting here anyways:
        //
        // If there are two `Arc`s remaining and two threads are running this code at the same, it
        // is not guaranteed that one will call `Arc::try_unwrap(inner)` and return `Ok(_)`. For
        // example one thread might run `Arc::try_unwrap(inner)` and return `Err(arc)`, then the
        // other thread runs `Arc::try_unwrap(inner)` and also returns `Err(arc)` since the `Arc` in
        // the first thread hasn't been dropped yet (it's contained in the `Err` return value). So
        // both remaining `Arc`s will be dropped without either `Arc::try_unwrap(inner)` ever
        // returning `Ok(_)`.

        // if this is the last reference, call close() on the file
        if let Ok(inner) = Arc::try_unwrap(inner) {
            Some(inner.close(cb_queue))
        } else {
            None
        }
    }
}

#[derive(Clone, Debug)]
struct OpenFileInner {
    file: Option<File>,
}

impl OpenFileInner {
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
        let _ = CallbackQueue::queue_and_run(|cb_queue| self.close_helper(cb_queue));
    }
}

bitflags::bitflags! {
    /// Flags for a file descriptor.
    ///
    /// Linux only supports a single descriptor flag:
    /// https://www.gnu.org/software/libc/manual/html_node/Descriptor-Flags.html
    pub struct DescriptorFlags: libc::c_int {
        const CLOEXEC = libc::FD_CLOEXEC;
    }
}

impl DescriptorFlags {
    pub fn as_o_flags(&self) -> OFlag {
        let mut flags = OFlag::empty();
        if self.contains(Self::CLOEXEC) {
            flags.insert(OFlag::O_CLOEXEC);
        }
        flags
    }

    /// Returns a tuple of the `DescriptorFlags` and any remaining flags.
    pub fn from_o_flags(flags: OFlag) -> (Self, OFlag) {
        let mut remaining = flags;
        let mut flags = Self::empty();

        if remaining.contains(OFlag::O_CLOEXEC) {
            remaining.remove(OFlag::O_CLOEXEC);
            flags.insert(Self::CLOEXEC);
        }

        (flags, remaining)
    }
}

/// A file descriptor that reference an open file. Also contains flags that change the behaviour of
/// this file descriptor.
#[derive(Debug)]
pub struct Descriptor {
    /// The file that this descriptor points to.
    file: CompatFile,
    /// Descriptor flags.
    flags: DescriptorFlags,
}

// will not compile if `Descriptor` is not Send + Sync
impl IsSend for Descriptor {}
impl IsSync for Descriptor {}

impl Descriptor {
    pub fn new(file: CompatFile) -> Self {
        Self {
            file,
            flags: DescriptorFlags::empty(),
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

    /// SAFETY: See `HostTreePointer::ptr`.
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
/// descriptor for that legacy file. This is similar to an `OpenFile` object, but does not close the
/// file when dropped since we don't have a pointer to the host. If this counter is dropped before
/// closing the legacy file, the legacy file will not be closed properly.
#[derive(Clone, Debug)]
pub struct LegacyFileCounter {
    file: CountedLegacyFileRef,
    /// A count of how many open descriptors there are with reference to this legacy file.
    open_count: Arc<()>,
}

impl LegacyFileCounter {
    pub fn new(file: CountedLegacyFileRef) -> Self {
        Self {
            file,
            open_count: Arc::new(()),
        }
    }

    pub unsafe fn ptr(&self) -> *mut c::LegacyFile {
        unsafe { self.file.ptr() }
    }

    /// Close the descriptor, and if this is the last descriptor pointing to its legacy file, close
    /// the legacy file as well.
    pub fn close(self, host: &Host) {
        // this isn't subject to race conditions since we should never access descriptors
        // from multiple threads at the same time
        if Arc::<()>::strong_count(&self.open_count) == 1 {
            unsafe { c::legacyfile_close(self.ptr(), host) }
        }
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

impl std::fmt::Debug for c::SysCallReturn {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut ds = f.debug_struct("SysCallReturn");
        ds.field("state", &self.state);
        match self.state {
            c::SysCallReturnState_SYSCALL_BLOCK => {
                ds.field("cond", unsafe { &self.u.blocked.cond });
                ds.field("restartable", unsafe { &self.u.blocked.restartable });
            }
            c::SysCallReturnState_SYSCALL_DONE => {
                ds.field("retval", unsafe { &self.u.done.retval });
                ds.field("restartable", unsafe { &self.u.done.restartable });
            }
            _ => {}
        };
        ds.finish()
    }
}

mod export {
    use super::*;

    /// The new descriptor takes ownership of the reference to the legacy file and does not
    /// increment its ref count, but will decrement the ref count when this descriptor is
    /// freed/dropped with `descriptor_free()`. The descriptor flags must be either 0 or
    /// `O_CLOEXEC`.
    #[no_mangle]
    pub unsafe extern "C" fn descriptor_fromLegacyFile(
        legacy_file: *mut c::LegacyFile,
        descriptor_flags: libc::c_int,
    ) -> *mut Descriptor {
        assert!(!legacy_file.is_null());

        let mut descriptor = Descriptor::new(CompatFile::Legacy(LegacyFileCounter::new(
            CountedLegacyFileRef::new(HostTreePointer::new(legacy_file)),
        )));

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
            unsafe { d.ptr() }
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

        unsafe { Box::from_raw(file as *mut OpenFile) };
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

        unsafe { Box::from_raw(file as *mut File) };
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
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host::syscall::Trigger;
    use crate::host::syscall_condition::SysCallCondition;
    use crate::host::syscall_types::{Blocked, Failed, SyscallError};

    #[test]
    // can't call foreign function: syscallcondition_new
    #[cfg_attr(miri, ignore)]
    fn test_syscallresult_roundtrip() {
        for val in vec![
            Ok(1.into()),
            Err(nix::errno::Errno::EPERM.into()),
            Err(SyscallError::Failed(Failed {
                errno: nix::errno::Errno::EINTR,
                restartable: true,
            })),
            Err(SyscallError::Failed(Failed {
                errno: nix::errno::Errno::EINTR,
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
            // roundtripping consumes the original value, and SysCallReturn doesn't implement Clone.
            // Compare their debug strings instead.
            let orig_debug = format!("{:?}", &val);
            let roundtripped = SyscallResult::from(c::SysCallReturn::from(val));
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
            c::SysCallReturn {
                state: c::SysCallReturnState_SYSCALL_DONE,
                u: c::SysCallReturnBody {
                    done: c::SysCallReturnDone {
                        retval: 1.into(),
                        restartable: false,
                    },
                },
            },
            c::SysCallReturn {
                state: c::SysCallReturnState_SYSCALL_BLOCK,
                u: c::SysCallReturnBody {
                    blocked: c::SysCallReturnBlocked {
                        cond: condition.into_inner(),
                        restartable: true,
                    },
                },
            },
            c::SysCallReturn {
                state: c::SysCallReturnState_SYSCALL_NATIVE,
                u: unsafe { std::mem::zeroed::<c::SysCallReturnBody>() },
            },
        ]
        .drain(..)
        {
            // We can't easily compare the value to the roundtripped result,
            // since roundtripping consumes the original value, and
            // SysCallReturn doesn't implement Clone. Compare their debug
            // strings instead.
            let orig_debug = format!("{:?}", &val);
            let roundtripped = c::SysCallReturn::from(SyscallResult::from(val));
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
