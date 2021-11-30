use std::convert::TryInto;
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::fcntl::OFlag;

use crate::cshadow as c;
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SyscallResult};
use crate::utility::event_queue::{EventQueue, EventSource, Handle};
use crate::utility::SyncSendPointer;

use socket::{SocketFile, SocketFileRef, SocketFileRefMut};

pub mod descriptor_table;
pub mod eventfd;
pub mod pipe;
pub mod shared_buf;
pub mod socket;

/// A trait we can use as a compile-time check to make sure that an object is Send.
trait IsSend: Send {}

/// A trait we can use as a compile-time check to make sure that an object is Sync.
trait IsSync: Sync {}

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
    pub struct FileState: libc::c_int {
        /// Has been initialized and it is now OK to unblock any plugin waiting
        /// on a particular state.
        const ACTIVE = c::_Status_STATUS_DESCRIPTOR_ACTIVE;
        /// Can be read, i.e. there is data waiting for user.
        const READABLE = c::_Status_STATUS_DESCRIPTOR_READABLE;
        /// Can be written, i.e. there is available buffer space.
        const WRITABLE = c::_Status_STATUS_DESCRIPTOR_WRITABLE;
        /// User already called close.
        const CLOSED = c::_Status_STATUS_DESCRIPTOR_CLOSED;
        /// A wakeup operation occurred on a futex.
        const FUTEX_WAKEUP = c::_Status_STATUS_FUTEX_WAKEUP;
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
struct LegacyListener(SyncSendPointer<c::StatusListener>);

impl LegacyListener {
    fn new(ptr: *mut c::StatusListener) -> Self {
        assert!(!ptr.is_null());
        unsafe { c::statuslistener_ref(ptr) };
        Self(SyncSendPointer(ptr))
    }

    fn ptr(&self) -> *mut c::StatusListener {
        self.0.ptr()
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
        ptr: *mut c::StatusListener,
        event_source: &mut EventSource<(FileState, FileState)>,
    ) {
        assert!(!ptr.is_null());

        // if it's already listening, don't add a second time
        if self.handle_map.contains_key(&(ptr as usize)) {
            return;
        }

        // this will ref the pointer and unref it when the closure is dropped
        let ptr_wrapper = LegacyListener::new(ptr);

        let handle = event_source.add_listener(move |(state, changed), _event_queue| unsafe {
            c::statuslistener_onStatusChanged(ptr_wrapper.ptr(), state.into(), changed.into())
        });

        // use a usize as the key so we don't accidentally deref the pointer
        self.handle_map.insert(ptr as usize, handle);
    }

    fn remove_listener(&mut self, ptr: *mut c::StatusListener) {
        assert!(!ptr.is_null());
        self.handle_map.remove(&(ptr as usize));
    }
}

/// A specified event source that passes a state and the changed bits to the function, but only if
/// the monitored bits have changed and if the change the filter is satisfied.
struct StateEventSource {
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
        notify_fn: impl Fn(FileState, FileState, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.inner
            .add_listener(move |(state, changed), event_queue| {
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

                (notify_fn)(state, changed, event_queue)
            })
    }

    pub fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.legacy_helper.add_listener(ptr, &mut self.inner);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.legacy_helper.remove_listener(ptr);
    }

    pub fn notify_listeners(
        &mut self,
        state: FileState,
        changed: FileState,
        event_queue: &mut EventQueue,
    ) {
        self.inner.notify_listeners((state, changed), event_queue)
    }
}

/// Represents a POSIX description, or a Linux "struct file".
#[derive(Clone)]
pub enum PosixFile {
    Pipe(Arc<AtomicRefCell<pipe::PipeFile>>),
    EventFd(Arc<AtomicRefCell<eventfd::EventFdFile>>),
    Socket(SocketFile),
}

// will not compile if `PosixFile` is not Send + Sync
impl IsSend for PosixFile {}
impl IsSync for PosixFile {}

impl PosixFile {
    pub fn borrow(&self) -> PosixFileRef {
        match self {
            Self::Pipe(ref f) => PosixFileRef::Pipe(f.borrow()),
            Self::EventFd(ref f) => PosixFileRef::EventFd(f.borrow()),
            Self::Socket(ref f) => PosixFileRef::Socket(f.borrow()),
        }
    }

    pub fn try_borrow(&self) -> Result<PosixFileRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::Pipe(ref f) => PosixFileRef::Pipe(f.try_borrow()?),
            Self::EventFd(ref f) => PosixFileRef::EventFd(f.try_borrow()?),
            Self::Socket(ref f) => PosixFileRef::Socket(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> PosixFileRefMut {
        match self {
            Self::Pipe(ref f) => PosixFileRefMut::Pipe(f.borrow_mut()),
            Self::EventFd(ref f) => PosixFileRefMut::EventFd(f.borrow_mut()),
            Self::Socket(ref f) => PosixFileRefMut::Socket(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<PosixFileRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::Pipe(ref f) => PosixFileRefMut::Pipe(f.try_borrow_mut()?),
            Self::EventFd(ref f) => PosixFileRefMut::EventFd(f.try_borrow_mut()?),
            Self::Socket(ref f) => PosixFileRefMut::Socket(f.try_borrow_mut()?),
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

impl std::fmt::Debug for PosixFile {
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

pub enum PosixFileRef<'a> {
    Pipe(atomic_refcell::AtomicRef<'a, pipe::PipeFile>),
    EventFd(atomic_refcell::AtomicRef<'a, eventfd::EventFdFile>),
    Socket(SocketFileRef<'a>),
}

pub enum PosixFileRefMut<'a> {
    Pipe(atomic_refcell::AtomicRefMut<'a, pipe::PipeFile>),
    EventFd(atomic_refcell::AtomicRefMut<'a, eventfd::EventFdFile>),
    Socket(SocketFileRefMut<'a>),
}

impl PosixFileRef<'_> {
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn get_status(&self) -> FileStatus
    );
}

impl PosixFileRefMut<'_> {
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Pipe, EventFd, Socket;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (event_queue), Pipe, EventFd, Socket;
        pub fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult
    );
    enum_passthrough!(self, (status), Pipe, EventFd, Socket;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), Pipe, EventFd, Socket;
        pub fn ioctl(&mut self, request: u64, arg_ptr: PluginPtr, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (ptr), Pipe, EventFd, Socket;
        pub fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );
    enum_passthrough!(self, (ptr), Pipe, EventFd, Socket;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );

    enum_passthrough_generic!(self, (bytes, offset, event_queue), Pipe, EventFd, Socket;
        pub fn read<W>(&mut self, bytes: W, offset: libc::off_t, event_queue: &mut EventQueue) -> SyscallResult
        where W: std::io::Write + std::io::Seek
    );

    enum_passthrough_generic!(self, (source, offset, event_queue), Pipe, EventFd, Socket;
        pub fn write<R>(&mut self, source: R, offset: libc::off_t, event_queue: &mut EventQueue) -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );
}

impl std::fmt::Debug for PosixFileRef<'_> {
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

impl std::fmt::Debug for PosixFileRefMut<'_> {
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

bitflags::bitflags! {
    // Linux only supports a single descriptor flag:
    // https://www.gnu.org/software/libc/manual/html_node/Descriptor-Flags.html
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

#[derive(Clone, Debug)]
pub struct Descriptor {
    /// The PosixFile that this descriptor points to.
    file: PosixFile,
    /// Descriptor flags.
    flags: DescriptorFlags,
    /// A count of how many open descriptors there are with reference to this file. Since a
    /// reference to the file can be held by other objects like an epoll file, it should
    /// be true that `Arc::strong_count(&self.count)` <= `Arc::strong_count(&self.file)`.
    open_count: Arc<()>,
}

impl Descriptor {
    pub fn new(file: PosixFile) -> Self {
        Self {
            file,
            flags: DescriptorFlags::empty(),
            open_count: Arc::new(()),
        }
    }

    pub fn get_file(&self) -> &PosixFile {
        &self.file
    }

    pub fn get_flags(&self) -> DescriptorFlags {
        self.flags
    }

    pub fn set_flags(&mut self, flags: DescriptorFlags) {
        self.flags = flags;
    }

    /// Close the descriptor, and if this is the last descriptor pointing to its file, close
    /// the file as well.
    pub fn close(self, event_queue: &mut EventQueue) -> Option<SyscallResult> {
        // this isn't subject to race conditions since we should never access descriptors
        // from multiple threads at the same time
        if Arc::<()>::strong_count(&self.open_count) == 1 {
            Some(self.file.borrow_mut().close(event_queue))
        } else {
            None
        }
    }

    /// Duplicate the descriptor, with both descriptors pointing to the same `PosixFile`. In
    /// Linux, the descriptor flags aren't typically copied to the new descriptor, so we
    /// explicitly require a flags value to avoid confusion.
    pub fn dup(&self, flags: DescriptorFlags) -> Self {
        let mut new_desc = self.clone();
        new_desc.set_flags(flags);
        new_desc
    }
}

/// Represents an owned reference to a legacy descriptor. Will decrement the descriptor's ref
/// count when dropped.
#[derive(Debug)]
pub struct OwnedLegacyDescriptor(SyncSendPointer<c::LegacyDescriptor>);

impl OwnedLegacyDescriptor {
    /// Does not increment the legacy descriptor's ref count, but will decrement the ref count
    /// when dropped.
    pub fn new(ptr: *mut c::LegacyDescriptor) -> Self {
        Self(SyncSendPointer(ptr))
    }

    pub fn ptr(&self) -> *mut c::LegacyDescriptor {
        self.0.ptr()
    }
}

impl Drop for OwnedLegacyDescriptor {
    fn drop(&mut self) {
        // unref the legacy descriptor object
        unsafe { c::descriptor_unref(self.0.ptr() as *mut core::ffi::c_void) };
    }
}

// don't implement copy or clone without considering the legacy descriptor's ref count
#[derive(Debug)]
pub enum CompatDescriptor {
    New(Descriptor),
    Legacy(OwnedLegacyDescriptor),
}

// will not compile if `CompatDescriptor` is not Send + Sync
impl IsSend for CompatDescriptor {}
impl IsSync for CompatDescriptor {}

impl CompatDescriptor {
    pub fn into_raw(descriptor: Box<Self>) -> *mut Self {
        Box::into_raw(descriptor)
    }

    pub fn from_raw(descriptor: *mut Self) -> Option<Box<Self>> {
        if descriptor.is_null() {
            return None;
        }

        unsafe { Some(Box::from_raw(descriptor)) }
    }

    /// Update the handle.
    /// This is a no-op for non-legacy descriptors.
    pub fn set_handle(&mut self, handle: Option<u32>) {
        if let CompatDescriptor::Legacy(d) = self {
            let handle = match handle {
                Some(x) => x.try_into().unwrap(),
                None => -1,
            };
            unsafe { c::descriptor_setHandle(d.ptr(), handle) }
        }
        // new descriptor types don't store their file handle, so do nothing
    }

    /// Close the descriptor. The `host` option is a legacy option for legacy descriptors.
    pub fn close(self, host: *mut c::Host, event_queue: &mut EventQueue) -> Option<SyscallResult> {
        match self {
            Self::New(desc) => desc.close(event_queue),
            Self::Legacy(desc) => {
                unsafe { c::descriptor_close(desc.ptr(), host) };
                Some(Ok(0.into()))
            }
        }
    }
}

mod export {
    use super::*;

    /// The new compat descriptor takes ownership of the reference to the legacy descriptor and
    /// does not increment its ref count, but will decrement the ref count when this compat
    /// descriptor is freed/dropped.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_fromLegacy(
        legacy_descriptor: *mut c::LegacyDescriptor,
    ) -> *mut CompatDescriptor {
        assert!(!legacy_descriptor.is_null());

        let descriptor = CompatDescriptor::Legacy(OwnedLegacyDescriptor::new(legacy_descriptor));
        CompatDescriptor::into_raw(Box::new(descriptor))
    }

    /// If the compat descriptor is a legacy descriptor, returns a pointer to the legacy
    /// descriptor object. Otherwise returns NULL. The legacy descriptor's ref count is not
    /// modified, so the pointer must not outlive the lifetime of the compat descriptor.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_asLegacy(
        descriptor: *const CompatDescriptor,
    ) -> *mut c::LegacyDescriptor {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        if let CompatDescriptor::Legacy(d) = descriptor {
            d.ptr()
        } else {
            std::ptr::null_mut()
        }
    }

    /// When the compat descriptor is freed/dropped, it will decrement the legacy descriptor's
    /// ref count.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_free(descriptor: *mut CompatDescriptor) {
        CompatDescriptor::from_raw(descriptor);
    }

    /// If the compat descriptor is a new descriptor, returns a pointer to the reference-counted
    /// posix file object. Otherwise returns NULL. The posix file object's ref count is not
    /// modified, so the pointer must not outlive the lifetime of the compat descriptor.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_borrowPosixFile(
        descriptor: *const CompatDescriptor,
    ) -> *const PosixFile {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        match descriptor {
            CompatDescriptor::Legacy(_) => std::ptr::null_mut(),
            CompatDescriptor::New(d) => d.get_file(),
        }
    }

    /// If the compat descriptor is a new descriptor, returns a pointer to the reference-counted
    /// posix file object. Otherwise returns NULL. The posix file object's ref count is
    /// incremented, so the pointer must always later be passed to `posixfile_drop()`, otherwise
    /// the memory will leak.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_newRefPosixFile(
        descriptor: *const CompatDescriptor,
    ) -> *const PosixFile {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        match descriptor {
            CompatDescriptor::Legacy(_) => std::ptr::null_mut(),
            CompatDescriptor::New(d) => Box::into_raw(Box::new(d.get_file().clone())),
        }
    }

    /// Decrement the ref count of the posix file object. The pointer must not be used after
    /// calling this function.
    #[no_mangle]
    pub extern "C" fn posixfile_drop(file: *const PosixFile) {
        assert!(!file.is_null());

        unsafe { Box::from_raw(file as *mut PosixFile) };
    }

    /// Get the state of the posix file object.
    #[allow(unused_variables)]
    #[no_mangle]
    pub extern "C" fn posixfile_getStatus(file: *const PosixFile) -> c::Status {
        assert!(!file.is_null());

        let file = unsafe { &*file };

        file.borrow().state().into()
    }

    /// Add a status listener to the posix file object. This will increment the status
    /// listener's ref count, and will decrement the ref count when this status listener is
    /// removed or when the posix file is freed/dropped.
    #[no_mangle]
    pub extern "C" fn posixfile_addListener(
        file: *const PosixFile,
        listener: *mut c::StatusListener,
    ) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = unsafe { &*file };

        file.borrow_mut().add_legacy_listener(listener);
    }

    /// Remove a listener from the posix file object.
    #[no_mangle]
    pub extern "C" fn posixfile_removeListener(
        file: *const PosixFile,
        listener: *mut c::StatusListener,
    ) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = unsafe { &*file };

        file.borrow_mut().remove_legacy_listener(listener);
    }

    /// Get the canonical handle for a posix file object. Two posix file objects refer to the same
    /// underlying data if their handles are equal.
    #[no_mangle]
    pub extern "C" fn posixfile_getCanonicalHandle(file: *const PosixFile) -> libc::uintptr_t {
        assert!(!file.is_null());

        let file = unsafe { &*file };

        file.canonical_handle()
    }
}

impl std::fmt::Debug for c::SysCallReturn {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SysCallReturn")
            .field("state", &self.state)
            .field("retval", &self.retval)
            .field("cond", &self.cond)
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host::syscall::Trigger;
    use crate::host::syscall_condition::SysCallCondition;
    use crate::host::syscall_types::SyscallError;

    #[test]
    fn test_syscallresult_roundtrip() {
        for val in vec![
            Ok(1.into()),
            Err(SyscallError::Errno(nix::errno::Errno::EPERM)),
            Err(SyscallError::Cond(SysCallCondition::new(Trigger::from(
                c::Trigger {
                    type_: 1,
                    object: c::TriggerObject {
                        as_pointer: std::ptr::null_mut(),
                    },
                    status: 2,
                },
            )))),
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
                retval: 1.into(),
                cond: std::ptr::null_mut(),
            },
            c::SysCallReturn {
                state: c::SysCallReturnState_SYSCALL_BLOCK,
                retval: 0.into(),
                cond: condition.into_inner(),
            },
            c::SysCallReturn {
                state: c::SysCallReturnState_SYSCALL_NATIVE,
                retval: 0.into(),
                cond: std::ptr::null_mut(),
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
