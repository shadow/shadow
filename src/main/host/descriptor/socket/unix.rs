use std::convert::TryFrom;
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;

use crate::cshadow as c;
use crate::host::descriptor::shared_buf::SharedBuf;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::descriptor::{
    FileMode, FileState, FileStatus, StateEventSource, StateListenerFilter, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::SysCallReg;
use crate::host::syscall_types::{PluginPtr, SyscallError};
use crate::utility::event_queue::{EventQueue, Handle};
use crate::utility::stream_len::StreamLen;

const UNIX_SOCKET_DEFAULT_BUFFER_SIZE: usize = 212_992;

pub struct UnixSocketFile {
    send_buffer: Option<Arc<AtomicRefCell<SharedBuf>>>,
    recv_buffer: Arc<AtomicRefCell<SharedBuf>>,
    event_source: StateEventSource,
    state: FileState,
    mode: FileMode,
    status: FileStatus,
    socket_type: UnixSocketType,
    peer_addr: Option<nix::sys::socket::UnixAddr>,
    bound_addr: Option<nix::sys::socket::UnixAddr>,
    namespace: Arc<AtomicRefCell<AbstractUnixNamespace>>,
    send_buffer_event_handle: Option<Handle<(FileState, FileState)>>,
    recv_buffer_event_handle: Option<Handle<(FileState, FileState)>>,
}

impl UnixSocketFile {
    pub fn new(
        mode: FileMode,
        status: FileStatus,
        socket_type: UnixSocketType,
        namespace: &Arc<AtomicRefCell<AbstractUnixNamespace>>,
    ) -> Arc<AtomicRefCell<Self>> {
        // must be able to both read and write to the socket
        assert!(mode.contains(FileMode::READ) && mode.contains(FileMode::WRITE));

        // initialize the socket's receive buffer
        let recv_buffer = SharedBuf::new(UNIX_SOCKET_DEFAULT_BUFFER_SIZE);
        let recv_buffer = Arc::new(AtomicRefCell::new(recv_buffer));

        let socket = Self {
            send_buffer: None,
            recv_buffer,
            event_source: StateEventSource::new(),
            state: FileState::ACTIVE,
            mode,
            status,
            socket_type,
            peer_addr: None,
            bound_addr: None,
            namespace: Arc::clone(namespace),
            send_buffer_event_handle: None,
            recv_buffer_event_handle: None,
        };

        let socket = Arc::new(AtomicRefCell::new(socket));
        let mut socket_ref = socket.borrow_mut();

        // update the socket's state when the buffer's state changes
        let weak = Arc::downgrade(&socket);
        let recv_handle = socket_ref.recv_buffer.borrow_mut().add_listener(
            FileState::READABLE,
            StateListenerFilter::Always,
            move |state, _changed, event_queue| {
                // if the file hasn't been dropped
                if let Some(socket) = weak.upgrade() {
                    let mut socket = socket.borrow_mut();

                    // if the socket is already closed, do nothing
                    if socket.state.contains(FileState::CLOSED) {
                        return;
                    }

                    // the socket is readable iff the buffer is readable
                    socket.copy_state(FileState::READABLE, state, event_queue);
                }
            },
        );

        socket_ref.recv_buffer_event_handle = Some(recv_handle);

        std::mem::drop(socket_ref);

        socket
    }

    pub fn get_status(&self) -> FileStatus {
        self.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.status = status;
    }

    pub fn mode(&self) -> FileMode {
        self.mode
    }

    pub fn get_bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.bound_addr
    }

    pub fn set_bound_address(
        &mut self,
        addr: Option<nix::sys::socket::UnixAddr>,
    ) -> Result<(), SyscallError> {
        // if already bound
        if self.bound_addr.is_some() {
            // TODO: not sure what should happen here
            return Err(Errno::EINVAL.into());
        }

        self.bound_addr = addr;
        Ok(())
    }

    pub fn get_peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.peer_addr
    }

    pub fn address_family(&self) -> nix::sys::socket::AddressFamily {
        nix::sys::socket::AddressFamily::Unix
    }

    pub fn recv_buffer(&self) -> &Arc<AtomicRefCell<SharedBuf>> {
        &self.recv_buffer
    }

    pub fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult {
        // drop the event listener handles so that we stop receiving new events
        self.send_buffer_event_handle
            .take()
            .map(|h| h.stop_listening());
        self.recv_buffer_event_handle
            .take()
            .map(|h| h.stop_listening());

        // inform the buffer that there is one fewer writers
        if let Some(send_buffer) = self.send_buffer.as_ref() {
            send_buffer.borrow_mut().remove_writer(event_queue);
        }

        // no need to hold on to the send buffer anymore
        self.send_buffer = None;

        // set the closed flag and remove the active, readable, and writable flags
        self.copy_state(
            FileState::CLOSED | FileState::ACTIVE | FileState::READABLE | FileState::WRITABLE,
            FileState::CLOSED,
            event_queue,
        );

        Ok(0.into())
    }

    pub fn bind(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        // if already bound
        if socket.borrow().bound_addr.is_some() {
            return Err(Errno::EINVAL.into());
        }

        // get the unix address
        let addr = match addr {
            Some(nix::sys::socket::SockAddr::Unix(x)) => x,
            _ => {
                log::warn!(
                    "Attempted to bind unix socket to non-unix address {:?}",
                    addr
                );
                return Err(Errno::EINVAL.into());
            }
        };

        // bind the socket
        if let Some(name) = addr.as_abstract() {
            // if given an abstract socket address
            let namespace = Arc::clone(&socket.borrow().namespace);
            if AbstractUnixNamespace::bind(&namespace, name.to_vec(), socket).is_err() {
                // address is in use
                return Err(Errno::EADDRINUSE.into());
            }
        } else if addr.path_len() == 0 {
            // if given an "unnamed" address
            let namespace = Arc::clone(&socket.borrow().namespace);
            if AbstractUnixNamespace::autobind(&namespace, socket, rng).is_err() {
                // no autobind addresses remaining
                return Err(Errno::EADDRINUSE.into());
            }
        } else {
            log::warn!("Only abstract names are currently supported for unix sockets");
            return Err(Errno::ENOTSUP.into());
        }

        socket.borrow_mut().bound_addr = Some(*addr);

        Ok(0.into())
    }

    pub fn read<W>(
        &mut self,
        mut _bytes: W,
        _offset: libc::off_t,
        _event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        W: std::io::Write + std::io::Seek,
    {
        // we could call UnixSocketFile::recvfrom() here, but for now we expect that there are no
        // code paths that would call UnixSocketFile::read() since the read() syscall handler should
        // have called UnixSocketFile::recvfrom() instead
        panic!("Called UnixSocketFile::read() on a unix socket.");
    }

    pub fn write<R>(
        &mut self,
        mut _bytes: R,
        _offset: libc::off_t,
        _event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        // we could call UnixSocketFile::sendto() here, but for now we expect that there are no code
        // paths that would call UnixSocketFile::write() since the write() syscall handler should
        // have called UnixSocketFile::sendto() instead
        panic!("Called UnixSocketFile::write() on a unix socket");
    }

    pub fn sendto<R>(
        &mut self,
        mut bytes: R,
        addr: Option<nix::sys::socket::SockAddr>,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        // if the file is not open for writing, return EBADF
        if !self.mode.contains(FileMode::WRITE) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let addr = match addr {
            Some(nix::sys::socket::SockAddr::Unix(x)) => Some(x),
            None => None,
            _ => return Err(Errno::EINVAL.into()),
        };

        // returns either the send buffer, or None if we should look up the send buffer from the
        // socket address
        let send_buffer = match (&self.send_buffer, addr) {
            // already connected but a destination address was given
            (Some(send_buffer), Some(_addr)) => match self.socket_type {
                UnixSocketType::Stream => return Err(Errno::EISCONN.into()),
                // linux seems to ignore the destination address for connected seq packet sockets
                UnixSocketType::SeqPacket => Some(send_buffer),
                UnixSocketType::Dgram => None,
            },
            // already connected and no destination address was given
            (Some(send_buffer), None) => Some(send_buffer),
            // not connected but a destination address was given
            (None, Some(_addr)) => match self.socket_type {
                UnixSocketType::Stream => return Err(Errno::EOPNOTSUPP.into()),
                UnixSocketType::SeqPacket => return Err(Errno::ENOTCONN.into()),
                UnixSocketType::Dgram => None,
            },
            // not connected and no destination address given
            (None, None) => return Err(Errno::ENOTCONN.into()),
        };

        // a variable for storing an Arc of the recv buffer if needed
        let mut _buf_arc_storage;

        // either use the existing send buffer, or look up the send buffer from the address
        let send_buffer = match send_buffer {
            Some(x) => x,
            None => {
                // if an abstract address
                if let Some(name) = addr.unwrap().as_abstract() {
                    // look up the socket from the address name
                    match self.namespace.borrow().lookup(name) {
                        // socket was found with the given name
                        Some(recv_socket) => {
                            // store an Arc of the recv buffer
                            _buf_arc_storage = Arc::clone(recv_socket.borrow().recv_buffer());
                            &_buf_arc_storage
                        }
                        // no socket has the given name
                        None => return Err(Errno::ECONNREFUSED.into()),
                    }
                } else {
                    log::warn!(
                        "Sending to pathname addresses from unix sockets is not yet supported"
                    );
                    return Err(Errno::ECONNREFUSED.into());
                }
            }
        };

        let mut send_buffer = send_buffer.borrow_mut();

        let len = bytes.stream_len_bp()? as usize;

        match self.socket_type {
            UnixSocketType::Stream => send_buffer.write_stream(bytes.by_ref(), len, event_queue),
            UnixSocketType::Dgram | UnixSocketType::SeqPacket => {
                send_buffer.write_packet(bytes.by_ref(), len, event_queue)?;
                Ok(len.into())
            }
        }
    }

    pub fn recvfrom<W>(
        &mut self,
        mut bytes: W,
        event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        // if the file is not open for reading, return EBADF
        if !self.mode.contains(FileMode::READ) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let mut recv_buffer = self.recv_buffer.borrow_mut();

        if !recv_buffer.has_data() {
            // return EWOULDBLOCK even if 'bytes' has length 0
            return Err(Errno::EWOULDBLOCK.into());
        }

        let num_read = recv_buffer.read(&mut bytes, event_queue)?;

        Ok((num_read.into(), None))
    }

    pub fn ioctl(
        &mut self,
        request: u64,
        _arg_ptr: PluginPtr,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        log::warn!(
            "We do not yet handle ioctl request {} on unix sockets",
            request
        );
        Err(Errno::EINVAL.into())
    }

    pub fn connect(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: nix::sys::socket::UnixAddr,
        send_buffer: Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) {
        let socket_ref = &mut *socket.borrow_mut();
        let mut send_buffer_ref = send_buffer.borrow_mut();

        // set the socket's peer address
        assert!(socket_ref.peer_addr.is_none());
        socket_ref.peer_addr = Some(addr);

        // increment the buffer's writer count
        send_buffer_ref.add_writer(event_queue);

        // update the socket file's state based on the buffer's state
        if send_buffer_ref.state().contains(FileState::WRITABLE) {
            socket_ref.state.insert(FileState::WRITABLE);
        }

        // update the socket's state when the buffer's state changes
        let weak = Arc::downgrade(&socket);
        let send_handle = send_buffer_ref.add_listener(
            FileState::WRITABLE,
            StateListenerFilter::Always,
            move |state, _changed, event_queue| {
                // if the file hasn't been dropped
                if let Some(socket) = weak.upgrade() {
                    let mut socket = socket.borrow_mut();

                    // if the socket is already closed, do nothing
                    if socket.state.contains(FileState::CLOSED) {
                        return;
                    }

                    // the socket is writable iff the buffer is writable
                    socket.copy_state(FileState::WRITABLE, state, event_queue);
                }
            },
        );

        std::mem::drop(send_buffer_ref);

        socket_ref.send_buffer = Some(send_buffer);
        socket_ref.send_buffer_event_handle = Some(send_handle);
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.state
    }

    fn copy_state(&mut self, mask: FileState, state: FileState, event_queue: &mut EventQueue) {
        let old_state = self.state;

        // remove the masked flags, then copy the masked flags
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, event_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, event_queue: &mut EventQueue) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, event_queue);
    }
}

#[derive(Copy, Clone, Debug)]
pub enum UnixSocketType {
    Stream,
    Dgram,
    SeqPacket,
}

impl TryFrom<libc::c_int> for UnixSocketType {
    type Error = UnixSocketTypeConversionError;
    fn try_from(val: libc::c_int) -> Result<Self, Self::Error> {
        match val {
            libc::SOCK_STREAM => Ok(Self::Stream),
            libc::SOCK_DGRAM => Ok(Self::Dgram),
            libc::SOCK_SEQPACKET => Ok(Self::SeqPacket),
            x => Err(UnixSocketTypeConversionError(x)),
        }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct UnixSocketTypeConversionError(libc::c_int);

impl std::error::Error for UnixSocketTypeConversionError {}

impl std::fmt::Display for UnixSocketTypeConversionError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(
            f,
            "Invalid socket type {}; unix sockets only support SOCK_STREAM, SOCK_DGRAM, and SOCK_SEQPACKET",
            self.0
        )
    }
}
