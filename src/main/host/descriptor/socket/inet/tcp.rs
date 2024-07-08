use std::net::{Ipv4Addr, SocketAddrV4};
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use bytes::BytesMut;
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use linux_api::socket::Shutdown;
use nix::sys::socket::{MsgFlags, SockaddrIn};
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::socket::inet;
use crate::host::descriptor::socket::{InetSocket, RecvmsgArgs, RecvmsgReturn, SendmsgArgs};
use crate::host::descriptor::{File, Socket};
use crate::host::descriptor::{
    FileMode, FileSignals, FileState, FileStatus, OpenFile, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::network::interface::FifoPacketPriority;
use crate::host::network::namespace::{AssociationHandle, NetworkNamespace};
use crate::host::syscall::io::{write_partial, IoVec, IoVecReader, IoVecWriter};
use crate::host::syscall::types::SyscallError;
use crate::network::packet::{PacketRc, PacketStatus};
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::{HostTreePointer, ObjectCounter};

pub struct TcpSocket {
    tcp_state: tcp::TcpState<TcpDeps>,
    socket_weak: Weak<AtomicRefCell<Self>>,
    event_source: StateEventSource,
    status: FileStatus,
    file_state: FileState,
    association: Option<AssociationHandle>,
    connect_result_is_pending: bool,
    shutdown_status: Option<Shutdown>,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
    _counter: ObjectCounter,
}

impl TcpSocket {
    pub fn new(status: FileStatus) -> Arc<AtomicRefCell<Self>> {
        let rv = Arc::new_cyclic(|weak: &Weak<AtomicRefCell<Self>>| {
            let tcp_dependencies = TcpDeps {
                timer_state: Arc::new(AtomicRefCell::new(TcpDepsTimerState {
                    socket: weak.clone(),
                    registered_by: tcp::TimerRegisteredBy::Parent,
                })),
            };

            AtomicRefCell::new(Self {
                tcp_state: tcp::TcpState::new(tcp_dependencies, tcp::TcpConfig::default()),
                socket_weak: weak.clone(),
                event_source: StateEventSource::new(),
                status,
                // the readable/writable file state shouldn't matter here since we run
                // `with_tcp_state` below to update it, but we need ACTIVE set so that epoll works
                file_state: FileState::ACTIVE,
                association: None,
                connect_result_is_pending: false,
                shutdown_status: None,
                has_open_file: false,
                _counter: ObjectCounter::new("TcpSocket"),
            })
        });

        // run a no-op function on the state, which will force the socket to update its file state
        // to match the tcp state
        CallbackQueue::queue_and_run(|cb_queue| {
            rv.borrow_mut().with_tcp_state(cb_queue, |_state| ())
        });

        rv
    }

    pub fn status(&self) -> FileStatus {
        self.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.status = status;
    }

    pub fn mode(&self) -> FileMode {
        FileMode::READ | FileMode::WRITE
    }

    pub fn has_open_file(&self) -> bool {
        self.has_open_file
    }

    pub fn supports_sa_restart(&self) -> bool {
        true
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.has_open_file = val;
    }

    fn with_tcp_state<T>(
        &mut self,
        cb_queue: &mut CallbackQueue,
        f: impl FnOnce(&mut tcp::TcpState<TcpDeps>) -> T,
    ) -> T {
        self.with_tcp_state_and_signal(cb_queue, |state| (f(state), FileSignals::empty()))
    }

    /// Update the current tcp state. The tcp state should only ever be updated through this method.
    fn with_tcp_state_and_signal<T>(
        &mut self,
        cb_queue: &mut CallbackQueue,
        f: impl FnOnce(&mut tcp::TcpState<TcpDeps>) -> (T, FileSignals),
    ) -> T {
        let rv = f(&mut self.tcp_state);

        // we may have mutated the tcp state, so update the socket's file state and notify listeners

        // if there are packets to send, notify the host
        if self.tcp_state.wants_to_send() {
            // The upgrade could fail if this was run during a drop, or if some outer code decided
            // to take the `TcpSocket` out of the `Arc` for some reason. Might as well panic since
            // it might indicate a bug somewhere else.
            let socket = self.socket_weak.upgrade().unwrap();

            // First try getting our IP address from the tcp state (if it's connected), then try
            // from the association handle (if it's not connected but is bound). Assume that our IP
            // address will match an interface's IP address.
            let interface_ip = *self
                .tcp_state
                .local_remote_addrs()
                .map(|x| x.0)
                .or(self.association.as_ref().map(|x| x.local_addr()))
                .unwrap()
                .ip();

            cb_queue.add(move |_cb_queue| {
                Worker::with_active_host(|host| {
                    let socket = InetSocket::Tcp(socket);
                    host.notify_socket_has_packets(interface_ip, &socket);
                })
                .unwrap();
            });
        }

        // the following mappings from `PollState` to `FileState` may be relied on by other parts of
        // the code, such as the `connect()` and `accept()` blocking behaviour, so be careful when
        // making changes

        let mut read_write_flags = FileState::empty();
        let poll_state = self.tcp_state.poll();

        if poll_state.intersects(tcp::PollState::READABLE | tcp::PollState::RECV_CLOSED) {
            read_write_flags.insert(FileState::READABLE);
        }
        if poll_state.intersects(tcp::PollState::WRITABLE) {
            read_write_flags.insert(FileState::WRITABLE);
        }
        if poll_state.intersects(tcp::PollState::READY_TO_ACCEPT) {
            read_write_flags.insert(FileState::READABLE);
        }
        if poll_state.intersects(tcp::PollState::ERROR) {
            read_write_flags.insert(FileState::READABLE | FileState::WRITABLE);
        }

        // if the socket/file is closed, undo all of the flags set above (closed sockets aren't
        // readable or writable)
        if self.file_state.contains(FileState::CLOSED) {
            read_write_flags = FileState::empty();
        }

        // overwrite readable/writable flags
        self.update_state(
            FileState::READABLE | FileState::WRITABLE,
            read_write_flags,
            rv.1,
            cb_queue,
        );

        // if the tcp state is in the closed state
        if poll_state.contains(tcp::PollState::CLOSED) {
            // drop the association handle so that we're removed from the network interface
            self.association = None;
            // we do not change to `FileState::CLOSED` here since that flag represents that the file
            // has closed (with `close()`), not that the tcp state has closed
        }

        rv.0
    }

    pub fn push_in_packet(
        &mut self,
        mut packet: PacketRc,
        cb_queue: &mut CallbackQueue,
        _recv_time: EmulatedTime,
    ) {
        packet.add_status(PacketStatus::RcvSocketProcessed);

        // TODO: don't bother copying the bytes if we know the push will fail

        // TODO: we have no way of adding `PacketStatus::RcvSocketDropped` if the tcp state drops
        // the packet

        let header = packet
            .get_tcp()
            .expect("TCP socket received a non-tcp packet");

        // in the future, the packet could contain an array of `Bytes` objects and we could simply
        // transfer the `Bytes` objects directly from the payload to the tcp state without copying
        // the bytes themselves

        let mut payload = BytesMut::zeroed(packet.payload_size());
        let num_bytes_copied = packet.get_payload(&mut payload);
        assert_eq!(num_bytes_copied, packet.payload_size());
        let payload = tcp::Payload(vec![payload.freeze()]);

        self.with_tcp_state_and_signal(cb_queue, |s| {
            let pushed_len = s.push_packet(&header, payload).unwrap();
            let signals = if pushed_len > 0 {
                FileSignals::READ_BUFFER_GREW
            } else {
                FileSignals::empty()
            };
            ((), signals)
        });

        packet.add_status(PacketStatus::RcvSocketBuffered);
    }

    pub fn pull_out_packet(&mut self, cb_queue: &mut CallbackQueue) -> Option<PacketRc> {
        #[cfg(debug_assertions)]
        let wants_to_send = self.tcp_state.wants_to_send();

        // make sure that `self.has_data_to_send()` agrees with `tcp_state.wants_to_send()`
        #[cfg(debug_assertions)]
        debug_assert_eq!(self.has_data_to_send(), wants_to_send);

        // pop a packet from the socket
        let rv = self.with_tcp_state(cb_queue, |s| s.pop_packet());

        let (header, payload) = match rv {
            Ok(x) => x,
            Err(tcp::PopPacketError::NoPacket) => {
                #[cfg(debug_assertions)]
                debug_assert!(!wants_to_send);
                return None;
            }
            Err(tcp::PopPacketError::InvalidState) => {
                #[cfg(debug_assertions)]
                debug_assert!(!wants_to_send);
                return None;
            }
        };

        #[cfg(debug_assertions)]
        debug_assert!(wants_to_send);

        let mut packet = PacketRc::new();

        // TODO: This is expensive. Here we allocate a new buffer, copy all of the payload bytes to
        // this new buffer, and then copy the bytes in this new buffer to the packet's buffer. In
        // the future, the packet could contain an array of `Bytes` objects and we could simply
        // transfer the `Bytes` objects directly from the tcp state's `Payload` object to the packet
        // without copying the bytes themselves.
        let payload = payload.concat();

        packet.set_tcp(&header);
        // TODO: set packet priority?
        packet.set_payload(&payload, /* priority= */ 0);
        packet.add_status(PacketStatus::SndCreated);

        Some(packet)
    }

    pub fn peek_next_packet_priority(&self) -> Option<FifoPacketPriority> {
        // TODO: support packet priorities?
        self.has_data_to_send().then_some(0)
    }

    pub fn has_data_to_send(&self) -> bool {
        self.tcp_state.wants_to_send()
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        // The socket state won't always have the local address. For example if the socket was bound
        // but connect() hasn't yet been called, the socket state will not have a local or remote
        // address. Instead we should get the local address from the association.
        Ok(Some(
            self.association
                .as_ref()
                .map(|x| x.local_addr().into())
                .unwrap_or(SockaddrIn::new(0, 0, 0, 0, 0)),
        ))
    }

    pub fn getpeername(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        // The association won't always have the peer address. For example if the socket was bound
        // before connect() was called, the association will have a peer of 0.0.0.0. Instead we
        // should get the peer address from the socket state.
        Ok(Some(
            self.tcp_state
                .local_remote_addrs()
                .map(|x| x.1.into())
                .ok_or(Errno::ENOTCONN)?,
        ))

        // TODO: This will not have the remote address once the tcp state has closed (for example by
        // `shutdown(RDWR)`), in which case `local_remote_addrs()` will return `None` so this will
        // incorrectly return ENOTCONN. Should fix this somehow and add a test.

        // TODO: I don't think `getpeername()` should not return a valid peer name before the
        // connection is successfully established.
    }

    pub fn address_family(&self) -> linux_api::socket::AddressFamily {
        linux_api::socket::AddressFamily::AF_INET
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        // we don't expect close() to ever have an error
        self.with_tcp_state(cb_queue, |state| state.close())
            .unwrap();

        // add the closed flag and remove all other flags
        self.update_state(
            FileState::all(),
            FileState::CLOSED,
            FileSignals::empty(),
            cb_queue,
        );

        Ok(())
    }

    pub fn bind(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: Option<&SockaddrStorage>,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        // if the address pointer was NULL
        let Some(addr) = addr else {
            return Err(Errno::EFAULT.into());
        };

        // if not an inet socket address
        let Some(addr) = addr.as_inet() else {
            return Err(Errno::EINVAL.into());
        };

        let addr: SocketAddrV4 = (*addr).into();

        let mut socket_ref = socket.borrow_mut();

        // if the socket is already associated
        if socket_ref.association.is_some() {
            return Err(Errno::EINVAL.into());
        }

        // this will allow us to receive packets from any peer
        let peer_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

        // associate the socket
        let (_addr, handle) = inet::associate_socket(
            InetSocket::Tcp(Arc::clone(socket)),
            addr,
            peer_addr,
            /* check_generic_peer= */ true,
            net_ns,
            rng,
        )?;

        socket_ref.association = Some(handle);

        Ok(0.into())
    }

    pub fn readv(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call TcpSocket::recvmsg() here, but for now we expect that there are no code
        // paths that would call TcpSocket::readv() since the readv() syscall handler should have
        // called TcpSocket::recvmsg() instead
        panic!("Called TcpSocket::readv() on a TCP socket");
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call TcpSocket::sendmsg() here, but for now we expect that there are no code
        // paths that would call TcpSocket::writev() since the writev() syscall handler should have
        // called TcpSocket::sendmsg() instead
        panic!("Called TcpSocket::writev() on a TCP socket");
    }

    pub fn sendmsg(
        socket: &Arc<AtomicRefCell<Self>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        let mut socket_ref = socket.borrow_mut();

        let Some(mut flags) = MsgFlags::from_bits(args.flags) else {
            log::debug!("Unrecognized send flags: {:#b}", args.flags);
            return Err(Errno::EINVAL.into());
        };

        if socket_ref.status().contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        let len: libc::size_t = args.iovs.iter().map(|x| x.len).sum();

        // run in a closure so that an early return doesn't skip checking if we should block
        let result = (|| {
            let reader = IoVecReader::new(args.iovs, mem);

            let rv = socket_ref.with_tcp_state(cb_queue, |state| state.send(reader, len));

            let num_sent = match rv {
                Ok(x) => x,
                Err(tcp::SendError::Full) => return Err(Errno::EWOULDBLOCK),
                Err(tcp::SendError::NotConnected) => return Err(Errno::EPIPE),
                Err(tcp::SendError::StreamClosed) => return Err(Errno::EPIPE),
                Err(tcp::SendError::Io(e)) => return Err(Errno::try_from(e).unwrap()),
                Err(tcp::SendError::InvalidState) => return Err(Errno::EINVAL),
            };

            Ok(num_sent)
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result == Err(Errno::EWOULDBLOCK) && !flags.contains(MsgFlags::MSG_DONTWAIT) {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Inet(InetSocket::Tcp(socket.clone()))),
                FileState::WRITABLE | FileState::CLOSED,
                socket_ref.supports_sa_restart(),
            ));
        }

        Ok(result?.try_into().unwrap())
    }

    pub fn recvmsg(
        socket: &Arc<AtomicRefCell<Self>>,
        args: RecvmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();

        // if there was an asynchronous error, return it
        if let Some(error) = socket_ref.with_tcp_state(cb_queue, |state| state.clear_error()) {
            // by returning this error, we're probably (but not necessarily) returning a previous
            // connect() result
            socket_ref.connect_result_is_pending = false;

            return Err(tcp_error_to_errno(error).into());
        }

        let Some(mut flags) = MsgFlags::from_bits(args.flags) else {
            log::debug!("Unrecognized recv flags: {:#b}", args.flags);
            return Err(Errno::EINVAL.into());
        };

        if socket_ref.status().contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        let len: libc::size_t = args.iovs.iter().map(|x| x.len).sum();

        // run in a closure so that an early return doesn't skip checking if we should block
        let result = (|| {
            let writer = IoVecWriter::new(args.iovs, mem);

            let rv = socket_ref.with_tcp_state(cb_queue, |state| state.recv(writer, len));

            let num_recv = match rv {
                Ok(x) => x,
                Err(tcp::RecvError::Empty) => {
                    if [Shutdown::SHUT_RD, Shutdown::SHUT_RDWR]
                        .map(Some)
                        .contains(&socket_ref.shutdown_status)
                    {
                        0
                    } else {
                        return Err(Errno::EWOULDBLOCK);
                    }
                }
                Err(tcp::RecvError::NotConnected) => return Err(Errno::ENOTCONN),
                Err(tcp::RecvError::StreamClosed) => 0,
                Err(tcp::RecvError::Io(e)) => return Err(Errno::try_from(e).unwrap()),
                Err(tcp::RecvError::InvalidState) => return Err(Errno::EINVAL),
            };

            Ok(RecvmsgReturn {
                return_val: num_recv.try_into().unwrap(),
                addr: None,
                msg_flags: MsgFlags::empty().bits(),
                control_len: 0,
            })
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Inet(InetSocket::Tcp(socket.clone()))),
                FileState::READABLE | FileState::CLOSED,
                socket_ref.supports_sa_restart(),
            ));
        }

        Ok(result?)
    }

    pub fn ioctl(
        &mut self,
        _request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _mem: &mut MemoryManager,
    ) -> SyscallResult {
        todo!();
    }

    pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError> {
        warn_once_then_debug!("We do not yet handle stat calls on tcp sockets");
        Err(Errno::EINVAL.into())
    }

    pub fn listen(
        socket: &Arc<AtomicRefCell<Self>>,
        backlog: i32,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();

        // linux also makes this cast, so negative backlogs wrap around to large positive backlogs
        // https://elixir.free-electrons.com/linux/v5.11.22/source/net/ipv4/af_inet.c#L212
        let backlog = backlog as u32;

        let is_associated = socket_ref.association.is_some();

        let rv = if is_associated {
            // if already associated, do nothing
            let associate_fn = || Ok(None);
            socket_ref.with_tcp_state(cb_queue, |state| state.listen(backlog, associate_fn))
        } else {
            // if not associated, associate and return the handle
            let associate_fn = || {
                // implicitly bind to all interfaces
                let local_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

                // want to receive packets from any address
                let peer_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);
                let socket = Arc::clone(socket);

                // associate the socket
                let (_addr, handle) = inet::associate_socket(
                    InetSocket::Tcp(Arc::clone(&socket)),
                    local_addr,
                    peer_addr,
                    /* check_generic_peer= */ true,
                    net_ns,
                    rng,
                )?;

                Ok::<_, SyscallError>(Some(handle))
            };
            socket_ref.with_tcp_state(cb_queue, |state| state.listen(backlog, associate_fn))
        };

        let handle = match rv {
            Ok(x) => x,
            Err(tcp::ListenError::InvalidState) => return Err(Errno::EINVAL.into()),
            Err(tcp::ListenError::FailedAssociation(e)) => return Err(e),
        };

        // the `associate_fn` may or may not have run, so `handle` may or may not be set
        if let Some(handle) = handle {
            assert!(socket_ref.association.is_none());
            socket_ref.association = Some(handle);
        }

        Ok(())
    }

    pub fn connect(
        socket: &Arc<AtomicRefCell<Self>>,
        peer_addr: &SockaddrStorage,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();

        // if there was an asynchronous error, return it
        if let Some(error) = socket_ref.with_tcp_state(cb_queue, |state| state.clear_error()) {
            // by returning this error, we're probably (but not necessarily) returning a previous
            // connect() result
            socket_ref.connect_result_is_pending = false;

            return Err(tcp_error_to_errno(error).into());
        }

        // if connect() had previously been called (either blocking or non-blocking), we need to
        // return the result
        if socket_ref.connect_result_is_pending {
            // ignore all connect arguments and just check if we've connected

            // check if it's still connecting (in the "syn-sent" or "syn-received" state)
            if socket_ref
                .tcp_state
                .poll()
                .contains(tcp::PollState::CONNECTING)
            {
                return Err(Errno::EALREADY.into());
            }

            // if not connecting and there were no socket errors (checked above)
            socket_ref.connect_result_is_pending = false;
            return Ok(());
        }

        // if not an inet socket address
        let Some(peer_addr) = peer_addr.as_inet() else {
            return Err(Errno::EINVAL.into());
        };

        let mut peer_addr: std::net::SocketAddrV4 = (*peer_addr).into();

        // On Linux a connection to 0.0.0.0 means a connection to localhost:
        // https://stackoverflow.com/a/22425796
        if peer_addr.ip().is_unspecified() {
            peer_addr.set_ip(std::net::Ipv4Addr::LOCALHOST);
        }

        let local_addr = socket_ref.association.as_ref().map(|x| x.local_addr());

        let rv = if let Some(mut local_addr) = local_addr {
            // the local address needs to be a specific address (this is normally what a routing
            // table would figure out for us)
            if local_addr.ip().is_unspecified() {
                if peer_addr.ip() == &std::net::Ipv4Addr::LOCALHOST {
                    local_addr.set_ip(Ipv4Addr::LOCALHOST)
                } else {
                    local_addr.set_ip(net_ns.default_ip)
                };
            }

            // it's already associated so use the existing address
            let associate_fn = || Ok((local_addr, None));
            socket_ref.with_tcp_state(cb_queue, |state| state.connect(peer_addr, associate_fn))
        } else {
            // if not associated, associate and return the handle
            let associate_fn = || {
                // the local address needs to be a specific address (this is normally what a routing
                // table would figure out for us)
                let local_addr = if peer_addr.ip() == &std::net::Ipv4Addr::LOCALHOST {
                    Ipv4Addr::LOCALHOST
                } else {
                    net_ns.default_ip
                };

                // add a wildcard port number
                let local_addr = SocketAddrV4::new(local_addr, 0);

                let (local_addr, handle) = inet::associate_socket(
                    InetSocket::Tcp(Arc::clone(socket)),
                    local_addr,
                    peer_addr,
                    /* check_generic_peer= */ true,
                    net_ns,
                    rng,
                )?;

                // use the actual local address that was assigned (will have port != 0)
                Ok((local_addr, Some(handle)))
            };
            socket_ref.with_tcp_state(cb_queue, |state| state.connect(peer_addr, associate_fn))
        };

        let handle = match rv {
            Ok(x) => x,
            Err(tcp::ConnectError::InProgress) => return Err(Errno::EALREADY.into()),
            Err(tcp::ConnectError::AlreadyConnected) => return Err(Errno::EISCONN.into()),
            Err(tcp::ConnectError::IsListening) => return Err(Errno::EISCONN.into()),
            Err(tcp::ConnectError::InvalidState) => return Err(Errno::EINVAL.into()),
            Err(tcp::ConnectError::FailedAssociation(e)) => return Err(e),
        };

        // the `associate_fn` may not have associated the socket, so `handle` may or may not be set
        if let Some(handle) = handle {
            assert!(socket_ref.association.is_none());
            socket_ref.association = Some(handle);
        }

        // we're attempting to connect, so set a flag so that we know a future connect() call should
        // return the result
        socket_ref.connect_result_is_pending = true;

        if socket_ref.status.contains(FileStatus::NONBLOCK) {
            Err(Errno::EINPROGRESS.into())
        } else {
            let err = SyscallError::new_blocked_on_file(
                File::Socket(Socket::Inet(InetSocket::Tcp(Arc::clone(socket)))),
                // I think we want this to resume when it leaves the "syn-sent" and "syn-received"
                // states (for example moves to the "rst", "closed", "fin-wait-1", etc states).
                //
                // - READABLE: the state may timeout in the "syn-received" state and move to the
                //   "closed" state, which is `tcp::PollState::RECV_CLOSED` and maps to
                //   `FileState::READABLE`
                // - WRITABLE: the state may reach the "established" state which is
                //   `tcp::PollState::WRITABLE` which maps to `FileState::WRITABLE`
                // - CLOSED: we use this just to be safe; typically the `connect()` syscall handler
                //   would hold an `OpenFile` for this socket while the syscall is blocked which
                //   would prevent the socket from being closed until the syscall completed
                //
                // We assume here that the "syn-sent" and "syn-received" states never have the
                // `RECV_CLOSED`, `READABLE`, or `WRITABLE` `PollState` states, otherwise this
                // syscall condition would trigger while the socket was still connecting. This all
                // relies on the `PollState` to `FileState` mappings in `with_tcp_state()` above.
                FileState::READABLE | FileState::WRITABLE | FileState::CLOSED,
                socket_ref.supports_sa_restart(),
            );

            // block the current thread
            Err(err)
        }
    }

    pub fn accept(
        &mut self,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        let rv = self.with_tcp_state(cb_queue, |state| state.accept());

        let accepted_state = match rv {
            Ok(x) => x,
            Err(tcp::AcceptError::InvalidState) => return Err(Errno::EINVAL.into()),
            Err(tcp::AcceptError::NothingToAccept) => return Err(Errno::EAGAIN.into()),
        };

        let local_addr = accepted_state.local_addr();
        let remote_addr = accepted_state.remote_addr();

        // convert the accepted tcp state to a full tcp socket
        let new_socket = Arc::new_cyclic(|weak: &Weak<AtomicRefCell<Self>>| {
            let accepted_state = accepted_state.finalize(|deps| {
                // update the timer state for new and existing pending timers to use the new
                // accepted socket rather than the parent listening socket
                let timer_state = &mut *deps.timer_state.borrow_mut();
                timer_state.socket = weak.clone();
                timer_state.registered_by = tcp::TimerRegisteredBy::Parent;
            });

            AtomicRefCell::new(Self {
                tcp_state: accepted_state,
                socket_weak: weak.clone(),
                event_source: StateEventSource::new(),
                status: FileStatus::empty(),
                // the readable/writable file state shouldn't matter here since we run
                // `with_tcp_state` below to update it, but we need ACTIVE set so that epoll works
                file_state: FileState::ACTIVE,
                association: None,
                connect_result_is_pending: false,
                shutdown_status: None,
                has_open_file: false,
                _counter: ObjectCounter::new("TcpSocket"),
            })
        });

        // run a no-op function on the state, which will force the socket to update its file state
        // to match the tcp state
        new_socket
            .borrow_mut()
            .with_tcp_state(cb_queue, |_state| ());

        // TODO: if the association fails, we lose the child socket

        // associate the socket
        let (_addr, handle) = inet::associate_socket(
            InetSocket::Tcp(Arc::clone(&new_socket)),
            local_addr,
            remote_addr,
            /* check_generic_peer= */ false,
            net_ns,
            rng,
        )?;

        new_socket.borrow_mut().association = Some(handle);

        Ok(OpenFile::new(File::Socket(Socket::Inet(InetSocket::Tcp(
            new_socket,
        )))))
    }

    pub fn shutdown(
        &mut self,
        how: Shutdown,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        // Update `how` based on any previous shutdown() calls. For example if shutdown(RD) was
        // previously called and now shutdown(WR) has been called, we should call shutdown(RDWR) on
        // the tcp state.
        let how = match (how, self.shutdown_status) {
            // if it was previously `SHUT_RDWR`
            (_, Some(Shutdown::SHUT_RDWR)) => Shutdown::SHUT_RDWR,
            // if it's now `SHUT_RDWR`
            (Shutdown::SHUT_RDWR, _) => Shutdown::SHUT_RDWR,
            (Shutdown::SHUT_RD, None | Some(Shutdown::SHUT_RD)) => Shutdown::SHUT_RD,
            (Shutdown::SHUT_RD, Some(Shutdown::SHUT_WR)) => Shutdown::SHUT_RDWR,
            (Shutdown::SHUT_WR, None | Some(Shutdown::SHUT_WR)) => Shutdown::SHUT_WR,
            (Shutdown::SHUT_WR, Some(Shutdown::SHUT_RD)) => Shutdown::SHUT_RDWR,
        };

        // Linux and the tcp library interpret shutdown flags differently. In the tcp library,
        // `tcp::Shutdown` has a very specific meaning for `SHUT_RD` and `SHUT_WR`, whereas Linux is
        // undocumented and not straightforward. Here we try to map from the Linux behaviour to the
        // tcp library behaviour.
        let tcp_how = match how {
            Shutdown::SHUT_RD => None,
            Shutdown::SHUT_WR => Some(tcp::Shutdown::Write),
            Shutdown::SHUT_RDWR => Some(tcp::Shutdown::Both),
        };

        if let Some(tcp_how) = tcp_how {
            if let Err(e) = self.with_tcp_state(cb_queue, |state| state.shutdown(tcp_how)) {
                match e {
                    tcp::ShutdownError::NotConnected => return Err(Errno::ENOTCONN.into()),
                    tcp::ShutdownError::InvalidState => return Err(Errno::EINVAL.into()),
                }
            }
        } else {
            // we don't need to call shutdown() on the tcp state since we don't actually want to do
            // anything, but we still need to return ENOTCONN sometimes

            let not_connected = !self
                .tcp_state
                .poll()
                .intersects(tcp::PollState::CONNECTING | tcp::PollState::CONNECTED);

            if not_connected {
                return Err(Errno::ENOTCONN.into());
            }
        }

        // the shutdown was successful, so update our shutdown status
        self.shutdown_status = Some(how);

        Ok(())
    }

    pub fn getsockopt(
        &mut self,
        level: libc::c_int,
        optname: libc::c_int,
        optval_ptr: ForeignPtr<()>,
        optlen: libc::socklen_t,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::socklen_t, SyscallError> {
        match (level, optname) {
            (libc::SOL_SOCKET, libc::SO_ERROR) => {
                // may update the socket's state (for example, reading `SO_ERROR` will make `poll()`
                // stop returning `POLLERR` for the socket)
                let error = self.with_tcp_state(cb_queue, |state| state.clear_error());
                let error = error.map(tcp_error_to_errno).map(Into::into).unwrap_or(0);

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &error, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_DOMAIN) => {
                let domain = libc::AF_INET;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &domain, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_TYPE) => {
                let sock_type = libc::SOCK_STREAM;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &sock_type, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_PROTOCOL) => {
                let protocol = libc::IPPROTO_TCP;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &protocol, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_ACCEPTCONN) => {
                let is_listener = self.tcp_state.poll().contains(tcp::PollState::LISTENING);
                let is_listener = is_listener as libc::c_int;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &is_listener, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            _ => {
                log_once_per_value_at_level!(
                    (level, optname),
                    (i32, i32),
                    log::Level::Warn,
                    log::Level::Debug,
                    "getsockopt called with unsupported level {level} and opt {optname}"
                );
                Err(Errno::ENOPROTOOPT.into())
            }
        }
    }

    pub fn setsockopt(
        &mut self,
        level: libc::c_int,
        optname: libc::c_int,
        _optval_ptr: ForeignPtr<()>,
        _optlen: libc::socklen_t,
        _mem: &MemoryManager,
    ) -> Result<(), SyscallError> {
        match (level, optname) {
            (libc::SOL_SOCKET, libc::SO_REUSEADDR) => {
                // TODO: implement this, tor and tgen use it
                log::trace!("setsockopt SO_REUSEADDR not yet implemented");
            }
            (libc::SOL_SOCKET, libc::SO_REUSEPORT) => {
                // TODO: implement this, tgen uses it
                log::trace!("setsockopt SO_REUSEPORT not yet implemented");
            }
            (libc::SOL_SOCKET, libc::SO_KEEPALIVE) => {
                // TODO: implement this, libevent uses it in evconnlistener_new_bind()
                log::trace!("setsockopt SO_KEEPALIVE not yet implemented");
            }
            (libc::SOL_SOCKET, libc::SO_BROADCAST) => {
                // TODO: implement this, pkg.go.dev/net uses it
                log::trace!("setsockopt SO_BROADCAST not yet implemented");
            }
            _ => {
                log_once_per_value_at_level!(
                    (level, optname),
                    (i32, i32),
                    log::Level::Warn,
                    log::Level::Debug,
                    "setsockopt called with unsupported level {level} and opt {optname}"
                );
                return Err(Errno::ENOPROTOOPT.into());
            }
        }

        Ok(())
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
        self.file_state
    }

    fn update_state(
        &mut self,
        mask: FileState,
        state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let old_state = self.file_state;

        // remove the masked flags, then copy the masked flags
        self.file_state.remove(mask);
        self.file_state.insert(state & mask);

        self.handle_state_change(old_state, signals, cb_queue);
    }

    fn handle_state_change(
        &mut self,
        old_state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let states_changed = self.file_state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() && signals.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.file_state, states_changed, signals, cb_queue);
    }
}

fn tcp_error_to_errno(error: tcp::TcpError) -> Errno {
    match error {
        tcp::TcpError::ResetSent => Errno::ECONNRESET,
        // TODO: when should this be ECONNREFUSED vs ECONNRESET? maybe we need more context?
        tcp::TcpError::ResetReceived => Errno::ECONNREFUSED,
        tcp::TcpError::ClosedWhileConnecting => Errno::ECONNRESET,
        tcp::TcpError::TimedOut => Errno::ETIMEDOUT,
    }
}

/// Shared state stored in timers. This allows us to update existing timers when a child `TcpState`
/// is accept()ed and becomes owned by a new `TcpSocket` object.
#[derive(Debug)]
struct TcpDepsTimerState {
    /// The socket that the timer callback will run on.
    socket: Weak<AtomicRefCell<TcpSocket>>,
    /// Whether the timer callback should modify the state of this socket
    /// ([`TimerRegisteredBy::Parent`]), or one of its child sockets ([`TimerRegisteredBy::Child`]).
    registered_by: tcp::TimerRegisteredBy,
}

/// The dependencies required by `TcpState::new()` so that the tcp code can interact with the
/// simulator.
#[derive(Debug)]
struct TcpDeps {
    /// State shared between all timers registered from this `TestEnvState`. This is needed since we
    /// may need to update existing pending timers when we accept() a `TcpState` from a listening
    /// state.
    timer_state: Arc<AtomicRefCell<TcpDepsTimerState>>,
}

impl tcp::Dependencies for TcpDeps {
    type Instant = EmulatedTime;
    type Duration = SimulationTime;

    fn register_timer(
        &self,
        time: Self::Instant,
        f: impl FnOnce(&mut tcp::TcpState<Self>, tcp::TimerRegisteredBy) + Send + Sync + 'static,
    ) {
        // make sure the socket is kept alive in the closure while the timer is waiting to be run
        // (don't store a weak reference), otherwise the socket may have already been dropped and
        // the timer won't run
        // TODO: is this the behaviour we want?
        let timer_state = self.timer_state.borrow();
        let socket = timer_state.socket.upgrade().unwrap();
        let registered_by = timer_state.registered_by;

        // This is needed because `TaskRef` takes a `Fn`, but we have a `FnOnce`. It would be nice
        // if we could schedule a task that is guaranteed to run only once so we could avoid this
        // extra allocation and atomic. Instead we'll panic if it does run more than once.
        let f = Arc::new(AtomicRefCell::new(Some(f)));

        // schedule a task with the host
        Worker::with_active_host(|host| {
            let task = TaskRef::new(move |_host| {
                // take ownership of the task; will panic if the task is run more than once
                let f = f.borrow_mut().take().unwrap();

                // run the original closure on the tcp state
                CallbackQueue::queue_and_run(|cb_queue| {
                    socket.borrow_mut().with_tcp_state(cb_queue, |state| {
                        f(state, registered_by);
                    })
                });
            });

            host.schedule_task_at_emulated_time(task, time);
        })
        .unwrap();
    }

    fn current_time(&self) -> Self::Instant {
        Worker::current_time().unwrap()
    }

    fn fork(&self) -> Self {
        let timer_state = self.timer_state.borrow();

        // if a child is trying to fork(), something has gone wrong
        assert_eq!(timer_state.registered_by, tcp::TimerRegisteredBy::Parent);

        Self {
            timer_state: Arc::new(AtomicRefCell::new(TcpDepsTimerState {
                socket: timer_state.socket.clone(),
                registered_by: tcp::TimerRegisteredBy::Child,
            })),
        }
    }
}
