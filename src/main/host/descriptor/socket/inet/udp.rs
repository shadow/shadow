use std::collections::LinkedList;
use std::io::{Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use bytes::{Bytes, BytesMut};
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use linux_api::socket::Shutdown;
use nix::sys::socket::{MsgFlags, SockaddrIn};
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::socket::inet::{self, InetSocket};
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, ShutdownFlags};
use crate::host::descriptor::{
    File, FileMode, FileSignals, FileState, FileStatus, OpenFile, Socket, SyscallResult,
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

/// Maximum size of a datagram we are allowed to send out over the network.
// 65,535 (2^16 - 1) - 20 (ip header) - 8 (udp header)
const CONFIG_DATAGRAM_MAX_SIZE: usize = 65507;

pub struct UdpSocket {
    event_source: StateEventSource,
    status: FileStatus,
    state: FileState,
    shutdown_status: ShutdownFlags,
    send_buffer: MessageBuffer<MessageSendHeader>,
    recv_buffer: MessageBuffer<MessageRecvHeader>,
    peer_addr: Option<SocketAddrV4>,
    bound_addr: Option<SocketAddrV4>,
    association: Option<AssociationHandle>,
    /// The receive time of the last packet returned to the managed process during a call to
    /// `recvmsg()`. Used for `SIOCGSTAMP`.
    recv_time_of_last_read_packet: Option<EmulatedTime>,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
    _counter: ObjectCounter,
}

impl UdpSocket {
    pub fn new(
        status: FileStatus,
        send_buf_size: usize,
        recv_buf_size: usize,
    ) -> Arc<AtomicRefCell<Self>> {
        let mut socket = Self {
            event_source: StateEventSource::new(),
            status,
            state: FileState::ACTIVE,
            shutdown_status: ShutdownFlags::empty(),
            send_buffer: MessageBuffer::new(send_buf_size),
            recv_buffer: MessageBuffer::new(recv_buf_size),
            peer_addr: None,
            bound_addr: None,
            association: None,
            recv_time_of_last_read_packet: None,
            has_open_file: false,
            _counter: ObjectCounter::new("UdpSocket"),
        };

        CallbackQueue::queue_and_run(|cb_queue| {
            socket.refresh_readable_writable(FileSignals::empty(), cb_queue)
        });

        Arc::new(AtomicRefCell::new(socket))
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

    pub fn push_in_packet(
        &mut self,
        mut packet: PacketRc,
        cb_queue: &mut CallbackQueue,
        recv_time: EmulatedTime,
    ) {
        packet.add_status(PacketStatus::RcvSocketProcessed);

        if let Some(peer_addr) = self.peer_addr {
            if peer_addr != packet.src_address() {
                // connect(2): "If the socket sockfd is of type SOCK_DGRAM, then addr is the address
                // to which datagrams are sent by default, and the only address from which datagrams
                // are received."

                // we have a peer, but received a packet from a different source address than that
                // peer
                packet.add_status(PacketStatus::RcvSocketDropped);

                // TODO: There's a race condition where we check the packet's address only when
                // receiving the packet from the network interface, but the user could call
                // `connect()` to set a peer after we've already received and buffered this packet.
                // My guess is that this race condition exists in Linux as well, but ideally we
                // should add a test, and do another check when `recvmsg()` is called if we really
                // need to.

                return;
            }
        };

        // TODO: also check the dst address to make sure we are the intended socket?

        // don't bother copying the bytes if we know the push will fail
        if !self.recv_buffer.has_space() {
            packet.add_status(PacketStatus::RcvSocketDropped);
            return;
        }

        // in the future, the packet could contain the `Bytes` object itself and we could simply
        // transfer the `Bytes` directly from the packet to the buffer without copying the bytes

        let mut message = BytesMut::zeroed(packet.payload_size());
        let num_bytes_copied = packet.get_payload(&mut message);
        assert_eq!(num_bytes_copied, packet.payload_size());

        let header = MessageRecvHeader {
            src: packet.src_address(),
            dst: packet.dst_address(),
            recv_time,
        };

        // push the message to the receive buffer (shouldn't fail since we checked for available
        // space above)
        self.recv_buffer
            .push_message(message.freeze(), header)
            .unwrap();

        log::trace!("Added a packet to the UDP socket's recv buffer");
        packet.add_status(PacketStatus::RcvSocketBuffered);

        self.refresh_readable_writable(FileSignals::READ_BUFFER_GREW, cb_queue);
    }

    pub fn pull_out_packet(&mut self, cb_queue: &mut CallbackQueue) -> Option<PacketRc> {
        // pop the message from the send buffer
        let Some((message, header)) = self.send_buffer.pop_message() else {
            log::debug!(
                "Attempted to remove a message from the UDP socket's send buffer, but none available"
            );

            return None;
        };

        log::trace!("Removed a message from the UDP socket's send buffer");

        let mut packet = PacketRc::new();
        let priority = header.packet_priority;

        // in the future, the packet could contain the `Bytes` object itself and we could simply
        // transfer the `Bytes` directly from the buffer to the packet without copying the bytes

        packet.set_udp(header.src, header.dst);
        packet.set_payload(&message, priority);
        packet.add_status(PacketStatus::SndCreated);

        self.refresh_readable_writable(FileSignals::empty(), cb_queue);

        Some(packet)
    }

    pub fn peek_next_packet_priority(&self) -> Option<FifoPacketPriority> {
        self.send_buffer.buffer.front().map(|x| x.1.packet_priority)
    }

    pub fn has_data_to_send(&self) -> bool {
        !self.send_buffer.is_empty()
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrIn>, Errno> {
        let mut addr = self
            .bound_addr
            .unwrap_or(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0));

        // if we are bound to INADDR_ANY, we should instead return the IP used to communicate with
        // the connected peer (if we have one)
        if *addr.ip() == Ipv4Addr::UNSPECIFIED {
            if let Some(peer_addr) = self.peer_addr {
                addr.set_ip(*peer_addr.ip());
            }
        }

        Ok(Some(addr.into()))
    }

    pub fn getpeername(&self) -> Result<Option<SockaddrIn>, Errno> {
        Ok(Some(self.peer_addr.ok_or(Errno::ENOTCONN)?.into()))
    }

    pub fn address_family(&self) -> linux_api::socket::AddressFamily {
        linux_api::socket::AddressFamily::AF_INET
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        // drop the existing association handle to disassociate the socket
        self.association = None;

        self.update_state(
            /* mask= */ FileState::all(),
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
    ) -> Result<(), SyscallError> {
        // if the address pointer was NULL
        let Some(addr) = addr else {
            return Err(Errno::EFAULT.into());
        };

        // if not an inet socket address
        let Some(addr) = addr.as_inet() else {
            return Err(Errno::EINVAL.into());
        };

        let addr: SocketAddrV4 = (*addr).into();

        {
            let socket = socket.borrow();

            // if the socket is already bound
            if socket.bound_addr.is_some() {
                return Err(Errno::EINVAL.into());
            }

            // Since we're not bound, we must not have a peer. We may have a peer in the future if
            // `connect()` is called on this socket.
            assert!(socket.peer_addr.is_none());

            // must not have been associated with the network interface
            assert!(socket.association.is_none());
        }

        // this will allow us to receive packets from any peer
        let unspecified_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

        // associate the socket
        let (addr, handle) = inet::associate_socket(
            InetSocket::Udp(Arc::clone(socket)),
            addr,
            unspecified_addr,
            /* check_generic_peer= */ true,
            net_ns,
            rng,
        )?;

        // update the socket's local address
        {
            let mut socket = socket.borrow_mut();
            socket.bound_addr = Some(addr);
            socket.association = Some(handle);
        }

        Ok(())
    }

    pub fn readv(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call UdpSocket::recvmsg() here, but for now we expect that there are no code
        // paths that would call UdpSocket::readv() since the readv() syscall handler should have
        // called UdpSocket::recvmsg() instead
        panic!("Called UdpSocket::readv() on a UDP socket");
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call UdpSocket::sendmsg() here, but for now we expect that there are no code
        // paths that would call UdpSocket::writev() since the writev() syscall handler should have
        // called UdpSocket::sendmsg() instead
        panic!("Called UdpSocket::writev() on a UDP socket");
    }

    pub fn sendmsg(
        socket: &Arc<AtomicRefCell<Self>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        let mut socket_ref = socket.borrow_mut();

        // if the file's writing has been shut down, return EPIPE
        if socket_ref.shutdown_status.contains(ShutdownFlags::WRITE) {
            return Err(linux_api::errno::Errno::EPIPE.into());
        }

        let Some(mut flags) = MsgFlags::from_bits(args.flags) else {
            log::debug!("Unrecognized send flags: {:#b}", args.flags);
            return Err(Errno::EINVAL.into());
        };

        let dst_addr = match args.addr {
            Some(addr) => match addr.as_inet() {
                // an inet socket address
                Some(x) => (*x).into(),
                // not an inet socket address
                None => return Err(Errno::EAFNOSUPPORT.into()),
            },
            // no destination address provided
            None => match socket_ref.peer_addr {
                Some(x) => x,
                None => return Err(Errno::EDESTADDRREQ.into()),
            },
        };

        if socket_ref.status().contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        let len: libc::size_t = args.iovs.iter().map(|x| x.len).sum();

        // TODO: should use IP fragmentation to make sure packets fit within the MTU
        if len > CONFIG_DATAGRAM_MAX_SIZE {
            return Err(linux_api::errno::Errno::EMSGSIZE.into());
        }

        // make sure that we're bound
        if socket_ref.bound_addr.is_some() {
            // we must have an association since we're bound
            assert!(socket_ref.association.is_some());
        } else {
            // we can't be unbound but have a peer
            assert!(socket_ref.peer_addr.is_none());
            assert!(socket_ref.association.is_none());

            // implicit bind (use default interface unless the remote peer is on loopback)
            // TODO: is this correct? or should we bind to UNSPECIFIED?
            let local_addr = if dst_addr.ip() == &std::net::Ipv4Addr::LOCALHOST {
                SocketAddrV4::new(Ipv4Addr::LOCALHOST, 0)
            } else {
                SocketAddrV4::new(net_ns.default_ip, 0)
            };

            // this will allow us to receive packets from any peer
            let unspecified_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

            let (local_addr, handle) = super::associate_socket(
                InetSocket::Udp(Arc::clone(socket)),
                local_addr,
                unspecified_addr,
                /* check_generic_peer= */ true,
                net_ns,
                rng,
            )?;

            socket_ref.bound_addr = Some(local_addr);
            socket_ref.association = Some(handle);
        }

        // run in a closure so that an early return doesn't skip checking if we should block
        let result = (|| {
            // don't bother copying the bytes if we know the push will fail
            if !socket_ref.send_buffer.has_space() {
                return Err(Errno::EWOULDBLOCK);
            }

            // write the iovs to an empty message
            let mut reader = IoVecReader::new(args.iovs, mem);
            let mut message = BytesMut::zeroed(len);
            reader
                .read_exact(&mut message[..])
                .map_err(|e| Errno::try_from(e).unwrap())?;

            // get the priority that we'll assign to the eventual packet
            let packet_priority =
                Worker::with_active_host(|host| host.get_next_packet_priority()).unwrap();

            let src_addr = socket_ref.bound_addr.unwrap();
            let src_addr = if src_addr.ip().is_unspecified() {
                // depending on the destination address, choose either localhost or the public IP
                // address
                if dst_addr.ip() == &std::net::Ipv4Addr::LOCALHOST {
                    SocketAddrV4::new(Ipv4Addr::LOCALHOST, src_addr.port())
                } else {
                    SocketAddrV4::new(net_ns.default_ip, src_addr.port())
                }
            } else {
                src_addr
            };

            let header = MessageSendHeader {
                src: src_addr,
                dst: dst_addr,
                packet_priority,
            };

            // push the message to the send buffer (shouldn't fail since we checked for available
            // space above)
            socket_ref
                .send_buffer
                .push_message(message.freeze(), header)
                .unwrap();

            // notify the host that this socket has packets to send
            let socket = Arc::clone(socket);
            let interface_ip = *socket_ref.bound_addr.unwrap().ip();
            cb_queue.add(move |_cb_queue| {
                Worker::with_active_host(|host| {
                    let socket = InetSocket::Udp(socket);
                    host.notify_socket_has_packets(interface_ip, &socket);
                })
                .unwrap();
            });

            Ok(len)
        })();

        socket_ref.refresh_readable_writable(FileSignals::empty(), cb_queue);

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result == Err(Errno::EWOULDBLOCK) && !flags.contains(MsgFlags::MSG_DONTWAIT) {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Inet(InetSocket::Udp(socket.clone()))),
                FileState::WRITABLE,
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
            // a temporary location to store the message and header if we popped them
            let message_storage;
            let header_storage;

            let (message, header) = if !flags.contains(MsgFlags::MSG_PEEK) {
                // pop the message from the receive buffer
                (message_storage, header_storage) = socket_ref
                    .recv_buffer
                    .pop_message()
                    .ok_or(Errno::EWOULDBLOCK)?;
                (&message_storage, &header_storage)
            } else {
                // peek the message from the receive buffer
                let (message, header) = socket_ref
                    .recv_buffer
                    .peek_message()
                    .ok_or(Errno::EWOULDBLOCK)?;
                (message, header)
            };

            // truncate the payload if the payload is larger than the user-provided buffers
            let truncated_message = &message[..std::cmp::min(len, message.len())];

            // write the truncated message to the iovs
            let mut writer = IoVecWriter::new(args.iovs, mem);
            writer
                .write_all(truncated_message)
                .map_err(|e| Errno::try_from(e).unwrap())?;

            let return_val = if flags.contains(MsgFlags::MSG_TRUNC) {
                message.len()
            } else {
                // the number of bytes written
                truncated_message.len()
            };

            let mut return_flags = MsgFlags::empty();
            return_flags.set(MsgFlags::MSG_TRUNC, truncated_message.len() < message.len());

            // update the cache of the last recv time
            socket_ref.recv_time_of_last_read_packet = Some(header.recv_time);

            Ok(RecvmsgReturn {
                return_val: return_val.try_into().unwrap(),
                addr: Some(header.src.into()),
                msg_flags: return_flags.bits(),
                control_len: 0,
            })
        })();

        socket_ref.refresh_readable_writable(FileSignals::empty(), cb_queue);

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            // if the syscall would block but the file's reading has been shut down, return EOF
            if socket_ref.shutdown_status.contains(ShutdownFlags::READ) {
                return Ok(RecvmsgReturn {
                    return_val: 0,
                    addr: None,
                    msg_flags: 0,
                    control_len: 0,
                });
            }

            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Inet(InetSocket::Udp(socket.clone()))),
                FileState::READABLE,
                socket_ref.supports_sa_restart(),
            ));
        }

        Ok(result?)
    }

    pub fn ioctl(
        &mut self,
        request: IoctlRequest,
        arg_ptr: ForeignPtr<()>,
        mem: &mut MemoryManager,
    ) -> SyscallResult {
        match request {
            // equivalent to SIOCINQ
            IoctlRequest::FIONREAD => {
                let len = self
                    .recv_buffer
                    .peek_message()
                    .map(|m| m.0.len())
                    .unwrap_or(0)
                    .try_into()
                    .unwrap();

                let arg_ptr = arg_ptr.cast::<libc::c_int>();
                mem.write(arg_ptr, &len)?;

                Ok(0.into())
            }
            // equivalent to SIOCOUTQ
            IoctlRequest::TIOCOUTQ => {
                let len = self.send_buffer.len_bytes().try_into().unwrap();

                let arg_ptr = arg_ptr.cast::<libc::c_int>();
                mem.write(arg_ptr, &len)?;

                Ok(0.into())
            }
            IoctlRequest::SIOCGSTAMP => {
                // socket(7): "Return a struct timeval with the receive timestamp of the last packet
                // passed to the user. [...] This ioctl should only be used if the socket option
                // SO_TIMESTAMP is not set on the socket. Otherwise, it returns the timestamp of the
                // last packet that was received while SO_TIMESTAMP was not set, or it fails if no
                // such packet has been received, (i.e., ioctl(2) returns -1 with errno set to
                // ENOENT)."
                let Some(last_recv_time) = self.recv_time_of_last_read_packet else {
                    return Err(Errno::ENOENT.into());
                };

                let last_recv_time = (last_recv_time - EmulatedTime::UNIX_EPOCH)
                    .try_into()
                    .unwrap();

                let arg_ptr = arg_ptr.cast::<libc::timeval>();
                mem.write(arg_ptr, &last_recv_time)?;

                Ok(0.into())
            }
            IoctlRequest::FIONBIO => {
                panic!("This should have been handled by the ioctl syscall handler");
            }
            IoctlRequest::TCGETS
            | IoctlRequest::TCSETS
            | IoctlRequest::TCSETSW
            | IoctlRequest::TCSETSF
            | IoctlRequest::TCGETA
            | IoctlRequest::TCSETA
            | IoctlRequest::TCSETAW
            | IoctlRequest::TCSETAF
            | IoctlRequest::TIOCGWINSZ
            | IoctlRequest::TIOCSWINSZ => {
                // not a terminal
                Err(Errno::ENOTTY.into())
            }
            request => {
                warn_once_then_debug!(
                    "We do not yet handle ioctl request {request:?} on tcp sockets"
                );
                Err(Errno::EINVAL.into())
            }
        }
    }

    pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError> {
        warn_once_then_debug!("We do not yet handle stat calls on udp sockets");
        Err(Errno::EINVAL.into())
    }

    pub fn listen(
        _socket: &Arc<AtomicRefCell<Self>>,
        _backlog: i32,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), Errno> {
        Err(Errno::EOPNOTSUPP)
    }

    pub fn connect(
        socket: &Arc<AtomicRefCell<Self>>,
        peer_addr: &SockaddrStorage,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        // if not an inet socket address
        // TODO: handle an AF_UNSPEC socket address
        let Some(peer_addr) = peer_addr.as_inet() else {
            return Err(Errno::EINVAL.into());
        };

        let mut peer_addr: std::net::SocketAddrV4 = (*peer_addr).into();

        // https://stackoverflow.com/a/22425796
        if peer_addr.ip().is_unspecified() {
            peer_addr.set_ip(std::net::Ipv4Addr::LOCALHOST);
        }

        // NOTE: it would be nice to use `Ipv4Addr::is_loopback` in this code rather than comparing
        // to `Ipv4Addr::LOCALHOST`, but the rest of Shadow probably can't handle other loopback
        // addresses (ex: 127.0.0.2) and it's probably best not to change this behaviour

        // make sure we will be able to route this later
        // TODO: UDP sockets probably shouldn't return `ECONNREFUSED`
        if peer_addr.ip() != &std::net::Ipv4Addr::LOCALHOST {
            let is_routable =
                Worker::is_routable(net_ns.default_ip.into(), (*peer_addr.ip()).into());

            if !is_routable {
                // can't route it - there is no node with this address
                log::warn!(
                    "Attempting to connect to address '{peer_addr}' for which no host exists"
                );
                return Err(Errno::ECONNREFUSED.into());
            }
        }

        // make sure that we're bound
        {
            let mut socket_ref = socket.borrow_mut();

            if let Some(bound_addr) = socket_ref.bound_addr {
                // we must have an association since we're bound
                assert!(socket_ref.association.is_some());

                // make sure the new peer address is connectable from the bound interface
                if !bound_addr.ip().is_unspecified() {
                    // assume that a socket bound to 0.0.0.0 can connect anywhere, so only check
                    // localhost
                    match (
                        bound_addr.ip() == &Ipv4Addr::LOCALHOST,
                        peer_addr.ip() == &Ipv4Addr::LOCALHOST,
                    ) {
                        // bound and peer on loopback interface
                        (true, true) => {}
                        // neither bound nor peer on loopback interface (shadow treats any
                        // non-127.0.0.1 address as an "internet" address)
                        (false, false) => {}
                        _ => return Err(Errno::EINVAL.into()),
                    }
                }
            } else {
                // we can't be unbound but have a peer
                assert!(socket_ref.peer_addr.is_none());
                assert!(socket_ref.association.is_none());

                // implicit bind (use default interface unless the remote peer is on loopback)
                let local_addr = if peer_addr.ip() == &std::net::Ipv4Addr::LOCALHOST {
                    SocketAddrV4::new(Ipv4Addr::LOCALHOST, 0)
                } else {
                    SocketAddrV4::new(net_ns.default_ip, 0)
                };

                // this will allow us to receive packets from any source address, but
                // `push_in_packet` should drop any packets that aren't from the peer
                let unspecified_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

                let (local_addr, handle) = super::associate_socket(
                    InetSocket::Udp(Arc::clone(socket)),
                    local_addr,
                    unspecified_addr,
                    /* check_generic_peer= */ true,
                    net_ns,
                    rng,
                )?;

                socket_ref.bound_addr = Some(local_addr);
                socket_ref.association = Some(handle);
            }

            socket_ref.peer_addr = Some(peer_addr);
        }

        Ok(())
    }

    pub fn accept(
        &mut self,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        Err(Errno::EOPNOTSUPP.into())
    }

    pub fn shutdown(
        &mut self,
        how: Shutdown,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        // TODO: what if we set a peer, then unset the peer, then call shutdown?
        if self.peer_addr.is_none() {
            return Err(Errno::ENOTCONN.into());
        }

        if how == Shutdown::SHUT_WR || how == Shutdown::SHUT_RDWR {
            // writing has been shut down
            self.shutdown_status.insert(ShutdownFlags::WRITE)
        }

        if how == Shutdown::SHUT_RD || how == Shutdown::SHUT_RDWR {
            // reading has been shut down
            self.shutdown_status.insert(ShutdownFlags::READ)
        }

        Ok(())
    }

    pub fn getsockopt(
        &mut self,
        level: libc::c_int,
        optname: libc::c_int,
        optval_ptr: ForeignPtr<()>,
        optlen: libc::socklen_t,
        mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::socklen_t, SyscallError> {
        match (level, optname) {
            (libc::SOL_SOCKET, libc::SO_SNDBUF) => {
                let sndbuf_size = self.send_buffer.soft_limit_bytes().try_into().unwrap();

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &sndbuf_size, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_RCVBUF) => {
                let rcvbuf_size = self.recv_buffer.soft_limit_bytes().try_into().unwrap();

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &rcvbuf_size, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_ERROR) => {
                let error = 0;

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
                let sock_type = libc::SOCK_DGRAM;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &sock_type, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_PROTOCOL) => {
                let protocol = libc::IPPROTO_UDP;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &protocol, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_ACCEPTCONN) => {
                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written = write_partial(mem, &0, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, _) => {
                log_once_per_value_at_level!(
                    (level, optname),
                    (i32, i32),
                    log::Level::Warn,
                    log::Level::Debug,
                    "getsockopt called with unsupported level {level} and opt {optname}"
                );
                Err(Errno::ENOPROTOOPT.into())
            }
            _ => {
                log_once_per_value_at_level!(
                    (level, optname),
                    (i32, i32),
                    log::Level::Warn,
                    log::Level::Debug,
                    "getsockopt called with unsupported level {level} and opt {optname}"
                );
                Err(Errno::EOPNOTSUPP.into())
            }
        }
    }

    pub fn setsockopt(
        &mut self,
        level: libc::c_int,
        optname: libc::c_int,
        optval_ptr: ForeignPtr<()>,
        optlen: libc::socklen_t,
        mem: &MemoryManager,
    ) -> Result<(), SyscallError> {
        match (level, optname) {
            (libc::SOL_SOCKET, libc::SO_SNDBUF) => {
                type OptType = libc::c_int;

                if usize::try_from(optlen).unwrap() < std::mem::size_of::<OptType>() {
                    return Err(Errno::EINVAL.into());
                }

                let optval_ptr = optval_ptr.cast::<OptType>();
                let val: u64 = mem.read(optval_ptr)?.try_into().or(Err(Errno::EINVAL))?;

                // linux kernel doubles this value upon setting
                let val = val * 2;

                // Linux also has limits SOCK_MIN_SNDBUF (slightly greater than 4096) and the sysctl
                // max limit. We choose a reasonable lower limit for Shadow. The minimum limit in
                // man 7 socket is incorrect.
                let val = std::cmp::max(val, 4096);

                // This upper limit was added as an arbitrarily high number so that we don't change
                // Shadow's behaviour, but also prevents an application from setting this to
                // something unnecessarily large like INT_MAX.
                let val = std::cmp::min(val, 268435456); // 2^28 = 256 MiB

                self.send_buffer
                    .set_soft_limit_bytes(val.try_into().unwrap());
            }
            (libc::SOL_SOCKET, libc::SO_RCVBUF) => {
                type OptType = libc::c_int;

                if usize::try_from(optlen).unwrap() < std::mem::size_of::<OptType>() {
                    return Err(Errno::EINVAL.into());
                }

                let optval_ptr = optval_ptr.cast::<OptType>();
                let val: u64 = mem.read(optval_ptr)?.try_into().or(Err(Errno::EINVAL))?;

                // linux kernel doubles this value upon setting
                let val = val * 2;

                // Linux also has limits SOCK_MIN_RCVBUF (slightly greater than 2048) and the sysctl
                // max limit. We choose a reasonable lower limit for Shadow. The minimum limit in
                // man 7 socket is incorrect.
                let val = std::cmp::max(val, 2048);

                // This upper limit was added as an arbitrarily high number so that we don't change
                // Shadow's behaviour, but also prevents an application from setting this to
                // something unnecessarily large like INT_MAX.
                let val = std::cmp::min(val, 268435456); // 2^28 = 256 MiB

                self.recv_buffer
                    .set_soft_limit_bytes(val.try_into().unwrap());
            }
            (libc::SOL_SOCKET, libc::SO_REUSEADDR) => {
                // TODO: implement this
                warn_once_then_debug!("setsockopt SO_REUSEADDR not yet implemented for udp");
                return Err(Errno::ENOPROTOOPT.into());
            }
            (libc::SOL_SOCKET, libc::SO_REUSEPORT) => {
                // TODO: implement this
                warn_once_then_debug!("setsockopt SO_REUSEPORT not yet implemented for udp");
                return Err(Errno::ENOPROTOOPT.into());
            }
            (libc::SOL_SOCKET, libc::SO_KEEPALIVE) => {
                // TODO: implement this
                warn_once_then_debug!("setsockopt SO_KEEPALIVE not yet implemented for udp");
                return Err(Errno::ENOPROTOOPT.into());
            }
            (libc::SOL_SOCKET, libc::SO_BROADCAST) => {
                // TODO: implement this, pkg.go.dev/net uses it
                warn_once_then_debug!(
                    "setsockopt SO_BROADCAST not yet implemented for udp; ignoring and returning 0"
                );
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
        self.state
    }

    fn refresh_readable_writable(&mut self, signals: FileSignals, cb_queue: &mut CallbackQueue) {
        let readable = !self.recv_buffer.is_empty();
        let writable = self.send_buffer.has_space();

        let readable = readable.then_some(FileState::READABLE).unwrap_or_default();
        let writable = writable.then_some(FileState::WRITABLE).unwrap_or_default();

        self.update_state(
            /* mask= */ FileState::READABLE | FileState::WRITABLE,
            readable | writable,
            signals,
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

        // remove the masked flags, then copy the masked flags
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

        // if nothing changed
        if states_changed.is_empty() && signals.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, signals, cb_queue);
    }
}

/// Non-payload data for a message in the send buffer.
#[derive(Debug)]
struct MessageSendHeader {
    /// The source address (typically the bind address). The application can theoretically use
    /// `IP_PKTINFO` to set a per-message source address.
    src: SocketAddrV4,
    /// The destination address (for example the peer).
    dst: SocketAddrV4,
    /// The priority for the packet that we'll create in the future, given to us by the host.
    packet_priority: FifoPacketPriority,
}

/// Non-payload data for a message in the receive buffer.
#[derive(Debug)]
struct MessageRecvHeader {
    /// The source address (for example the peer).
    src: SocketAddrV4,
    /// The destination address (typically the bind address). The application can theoretically use
    /// `IP_PKTINFO` to get the packet destination address.
    #[allow(dead_code)]
    dst: SocketAddrV4,
    /// The time when the network interface received the message.
    recv_time: EmulatedTime,
}

/// A buffer of UDP messages and message headers.
#[derive(Debug)]
struct MessageBuffer<Hdr> {
    /// The message payloads and headers.
    // use a `LinkedList` so that socket buffers can shrink when they're empty (as opposed to
    // `VecDeque`)
    buffer: LinkedList<(Bytes, Hdr)>,
    /// The number of payload bytes in this socket.
    len_bytes: usize,
    /// A soft limit for the maximum number of payload bytes this buffer can hold.
    soft_limit_bytes: usize,
}

impl<Hdr> MessageBuffer<Hdr> {
    pub fn new(soft_limit_bytes: usize) -> Self {
        Self {
            buffer: std::collections::LinkedList::new(),
            len_bytes: 0,
            soft_limit_bytes,
        }
    }

    /// Push a message to the buffer. Returns the message and header as an `Err` if there wasn't
    /// enough space.
    pub fn push_message(&mut self, message: Bytes, header: Hdr) -> Result<(), (Bytes, Hdr)> {
        // TODO: i think udp allows at most one packet to exceed the buffer capacity; should confirm
        // this
        if !self.has_space() {
            return Err((message, header));
        }

        // TODO: on linux the socket buffer length also takes into account any header and struct
        // overhead, otherwise the buffer would take an infinite amount of 0-len packets
        self.len_bytes += message.len();
        self.buffer.push_back((message, header));

        Ok(())
    }

    /// Pop the next message from the buffer. Returns a tuple of the message bytes and message
    /// header.
    pub fn pop_message(&mut self) -> Option<(Bytes, Hdr)> {
        let (message, header) = self.buffer.pop_front()?;
        self.len_bytes -= message.len();

        Some((message, header))
    }

    /// Peek the next message in the buffer.
    pub fn peek_message(&self) -> Option<&(Bytes, Hdr)> {
        self.buffer.front()
    }

    /// The number of payload bytes contained in the buffer. A length of 0 does not mean that the
    /// buffer is empty.
    pub fn len_bytes(&self) -> usize {
        self.len_bytes
    }

    /// Is there space for at least one more packet?
    pub fn has_space(&self) -> bool {
        self.len_bytes < self.soft_limit_bytes
    }

    /// Is the buffer empty (does it have 0 packets)?
    pub fn is_empty(&self) -> bool {
        self.buffer.is_empty()
    }

    /// The soft limit for the size of the buffer.
    pub fn soft_limit_bytes(&self) -> usize {
        self.soft_limit_bytes
    }

    /// Set the soft limit for the size of the buffer.
    pub fn set_soft_limit_bytes(&mut self, soft_limit_bytes: usize) {
        self.soft_limit_bytes = soft_limit_bytes;
    }
}
