use std::collections::{HashMap, LinkedList};
use std::io::{Read, Write};
use std::net::SocketAddrV4;

use crate::buffer::RecvQueue;
use crate::connection::Connection;
use crate::seq::Seq;
use crate::util::remove_from_list;
use crate::util::time::Duration;
use crate::{
    AcceptError, AcceptedTcpState, CloseError, ConnectError, Dependencies, ListenError, Payload,
    PollState, PopPacketError, PushPacketError, RecvError, RstCloseError, SendError, Shutdown,
    ShutdownError, TcpConfig, TcpError, TcpFlags, TcpHeader, TcpState, TcpStateEnum, TcpStateTrait,
    TimerRegisteredBy,
};

// state structs

/// The initial state of the TCP socket. While it's not a part of the official TCP state diagram, we
/// don't want to overload the "closed" state to mean both a closed socket and a never used socket,
/// since we don't allow TCP socket re-use.
#[derive(Debug)]
pub struct InitState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) config: TcpConfig,
}

#[derive(Debug)]
pub struct ListenState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) config: TcpConfig,
    pub(crate) max_backlog: u32,
    pub(crate) send_buffer: LinkedList<TcpHeader>,
    /// Child TCP states.
    ///
    /// Child states should only be mutated through the [`with_child`](Self::with_child) method to
    /// ensure that this parent stays in sync with the child.
    pub(crate) children: slotmap::DenseSlotMap<ChildTcpKey, ChildEntry<X>>,
    /// A map from 4 tuple (source address, destination address) to child. Packets received from the
    /// source address will be forwarded to the child.
    pub(crate) conn_map: HashMap<RemoteLocalPair, ChildTcpKey>,
    /// A queue of child TCP states in the "established" state, ready to be accept()ed.
    pub(crate) accept_queue: LinkedList<ChildTcpKey>,
    /// A list of child TCP states that want to send a packet.
    pub(crate) to_send: LinkedList<ChildTcpKey>,
}

#[derive(Debug)]
pub struct SynSentState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct SynReceivedState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct EstablishedState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct FinWaitOneState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct FinWaitTwoState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct ClosingState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct TimeWaitState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct CloseWaitState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

#[derive(Debug)]
pub struct LastAckState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) connection: Connection<X::Instant>,
}

/// A state for sockets that need to send RST packets before closing. While it's not a part of the
/// official TCP state diagram, we need to be able to buffer RST packets to send. We can't buffer
/// RST packets in the "closed" state since the "closed" state is not allowed to send packets, so we
/// use this as an intermediate state before we move to the "closed" state. We may need to buffer
/// several RST packets; for example states in the "listening" state might need to send an RST
/// packet for each child.
#[derive(Debug)]
pub struct RstState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) send_buffer: LinkedList<TcpHeader>,
    /// Was the socket previously connected? Should be `true` for any states that have previously
    /// been in the "syn-sent" or "syn-received" states. The connection does not need to have been
    /// successful (for example it may have timed out in the "syn-sent" state or may have been
    /// reset).
    pub(crate) was_connected: bool,
}

#[derive(Debug)]
pub struct ClosedState<X: Dependencies> {
    pub(crate) common: Common<X>,
    pub(crate) recv_buffer: RecvQueue,
    /// Was the socket previously connected? Should be `true` for any states that have previously
    /// been in the "syn-sent" or "syn-received" states. The connection does not need to have been
    /// successful (for example it may have timed out in the "syn-sent" state or may have been
    /// reset).
    pub(crate) was_connected: bool,
}

// other helper types

/// Indicates that no child exists for the given [key](ChildTcpKey).
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
struct ChildNotFound;

#[derive(Debug)]
pub(crate) struct Common<X: Dependencies> {
    pub(crate) deps: X,
    /// If the current state is a child of a parent state, this should be the key that the parent
    /// can use to lookup ths child state.
    pub(crate) child_key: Option<ChildTcpKey>,
    pub(crate) error: Option<TcpError>,
}

impl<X: Dependencies> Common<X> {
    /// Register a timer for this state.
    ///
    /// This method will make sure that the callback gets run on the correct state, even if called
    /// by a child state.
    pub fn register_timer(
        &self,
        time: X::Instant,
        f: impl FnOnce(TcpStateEnum<X>) -> TcpStateEnum<X> + Send + Sync + 'static,
    ) {
        // the handle that identifies this state if the state is a child of some parent state
        let child_key = self.child_key;

        // takes an owned `TcpStateEnum` and returns a `TcpStateEnum`
        let timer_cb_inner = move |mut parent_state, state_type| {
            match state_type {
                // we're the parent and the timer was registered by us
                TimerRegisteredBy::Parent => f(parent_state),
                // we're the parent and the timer was registered by a child
                TimerRegisteredBy::Child => {
                    // if not in the listening state anymore, then the child must not exist
                    let TcpStateEnum::Listen(parent_listen_state) = &mut parent_state else {
                        // do nothing
                        return parent_state;
                    };

                    // we need to lookup the child in `state` and run f() on the child's state
                    // instead

                    let child_key = child_key.expect(
                        "The timer was supposedly registered by a child state, but there was no \
                        key to identify the child",
                    );

                    let rv = parent_listen_state.with_child(child_key, |state| (f(state), ()));

                    // in practice we want to ignore the error, but by doing a match here we make
                    // sure that if the return type of `with_child` changes in the future, this code
                    // will break and we can update it
                    #[allow(clippy::single_match)]
                    match rv {
                        Ok(()) => {}
                        // we ignore this since the child may have been closed
                        Err(ChildNotFound) => {}
                    }

                    parent_state
                }
            }
        };

        // mutates a reference to a `TcpState` (this is a separate closure since it saves us two
        // levels of indentation in the inner closure above)
        let timer_cb = move |parent_state: &mut TcpState<X>, state_type| {
            parent_state.with_state(|state| (timer_cb_inner(state, state_type), ()))
        };

        self.deps.register_timer(time, timer_cb);
    }

    pub fn current_time(&self) -> X::Instant {
        self.deps.current_time()
    }

    /// Returns true if the error was set, or false if the error was previously set and was not
    /// modified.
    pub fn set_error_if_unset(&mut self, new_error: TcpError) -> bool {
        if self.error.is_none() {
            self.error = Some(new_error);
            return true;
        }

        false
    }
}

/// A pair of remote and local addresses, typically used to represent a connection (the 4-tuple).
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub(crate) struct RemoteLocalPair {
    /// The remote address (where a received packet was addressed from, or the address we're sending
    /// a packet to).
    remote: SocketAddrV4,
    /// The local address (where a received packet was addressed to, or the address we're sending a
    /// packet from).
    local: SocketAddrV4,
}

impl RemoteLocalPair {
    pub fn new(remote: SocketAddrV4, local: SocketAddrV4) -> Self {
        Self { remote, local }
    }
}

slotmap::new_key_type! { pub(crate) struct ChildTcpKey; }

#[derive(Debug)]
pub(crate) struct ChildEntry<X: Dependencies> {
    /// The `Option` is required so that we can run [`TcpState`] methods that require `self`, for
    /// example `child.push_packet()`.
    state: Option<TcpStateEnum<X>>,
    conn_addrs: RemoteLocalPair,
}

// state implementations

impl<X: Dependencies> InitState<X> {
    pub fn new(deps: X, config: TcpConfig) -> Self {
        let common = Common {
            deps,
            child_key: None,
            error: None,
        };

        InitState { common, config }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for InitState<X> {
    fn close(self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = ClosedState::new(self.common, None, /* was_connected= */ false);
        (new_state.into(), Ok(()))
    }

    fn rst_close(self) -> (TcpStateEnum<X>, Result<(), RstCloseError>) {
        // no need to send a RST; closing immediately
        let new_state = ClosedState::new(self.common, None, /* was_connected= */ false);
        (new_state.into(), Ok(()))
    }

    fn shutdown(self, _how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        (self.into(), Err(ShutdownError::NotConnected))
    }

    fn listen<T, E>(
        self,
        backlog: u32,
        associate_fn: impl FnOnce() -> Result<T, E>,
    ) -> (TcpStateEnum<X>, Result<T, ListenError<E>>) {
        let rv = match associate_fn() {
            Ok(x) => x,
            Err(e) => return (self.into(), Err(ListenError::FailedAssociation(e))),
        };

        // linux uses a queue limit of one greater than the provided backlog
        let max_backlog = backlog.saturating_add(1);

        let new_state = ListenState::new(self.common, self.config, max_backlog);
        (new_state.into(), Ok(rv))
    }

    fn connect<T, E>(
        self,
        remote_addr: SocketAddrV4,
        associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        let assoc_result = associate_fn();

        let (local_addr, assoc_result) = match assoc_result {
            Ok((local_addr, assoc_result)) => (local_addr, assoc_result),
            Err(e) => return (self.into(), Err(ConnectError::FailedAssociation(e))),
        };

        assert!(!local_addr.ip().is_unspecified());

        let connection = Connection::new(local_addr, remote_addr, Seq::new(0), self.config);

        let new_state = SynSentState::new(self.common, connection);
        (new_state.into(), Ok(assoc_result))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::NotConnected))
    }

    fn recv(self, _writer: impl Write, _len: usize) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        (self.into(), Err(RecvError::NotConnected))
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::empty();

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        false
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        None
    }
}

impl<X: Dependencies> ListenState<X> {
    fn new(common: Common<X>, config: TcpConfig, max_backlog: u32) -> Self {
        ListenState {
            common,
            config,
            max_backlog,
            send_buffer: LinkedList::new(),
            children: slotmap::DenseSlotMap::with_key(),
            conn_map: HashMap::new(),
            accept_queue: LinkedList::new(),
            to_send: LinkedList::new(),
        }
    }

    /// Register a new child TCP state for a new incoming connection.
    fn register_child(&mut self, header: &TcpHeader, payload: Payload) -> ChildTcpKey {
        let conn_addrs = RemoteLocalPair::new(header.src(), header.dst());

        let key = self.children.insert_with_key(|key| {
            let common = Common {
                deps: self.common.deps.fork(),
                child_key: Some(key),
                error: None,
            };

            assert!(header.flags.contains(TcpFlags::SYN));
            assert!(!header.flags.contains(TcpFlags::RST));

            let mut connection =
                Connection::new(header.dst(), header.src(), Seq::new(0), self.config);
            connection.push_packet(header, payload).unwrap();

            let new_tcp = SynReceivedState::new(common, connection);

            ChildEntry {
                state: Some(new_tcp.into()),
                conn_addrs,
            }
        });

        assert!(self.conn_map.insert(conn_addrs, key).is_none());

        // make sure the child is added to all of the correct lists
        self.sync_child(key).unwrap();

        key
    }

    /// Make sure the parent's state is synchronized with the child's state. For example if the
    /// child is in the "established" state, it should be in the parent's accept queue.
    fn sync_child(&mut self, key: ChildTcpKey) -> Result<(), ChildNotFound> {
        let is_closed;

        {
            let entry = self.children.get_mut(key).ok_or(ChildNotFound)?;
            let child = &mut entry.state;
            let conn_addrs = &entry.conn_addrs;

            // add to or remove from the `to_send` list
            if child.as_ref().unwrap().wants_to_send() {
                // if it wants to send a packet but is not in the `to_send` list
                if !self.to_send.contains(&key) {
                    // add to the `to_send` list
                    self.to_send.push_back(key);
                }
            } else {
                // doesn't want to send a packet, remove from the `to_send` list
                remove_from_list(&mut self.to_send, &key);
            }

            // add to or remove from the accept queue
            if matches!(
                child.as_ref().unwrap(),
                TcpStateEnum::Established(_) | TcpStateEnum::CloseWait(_)
            ) {
                // if in the "established" or "close-wait" state, but not in the accept queue
                if !self.accept_queue.contains(&key) {
                    // add to the accept queue
                    self.accept_queue.push_back(key);
                }
            } else {
                // not in the "established" or "close-wait" state; remove from the accept queue
                remove_from_list(&mut self.accept_queue, &key);
            }

            // make sure that it's contained in the src map
            assert!(self.conn_map.contains_key(conn_addrs));
            debug_assert_eq!(self.conn_map.get(conn_addrs).unwrap(), &key);

            is_closed = child.as_ref().unwrap().poll().contains(PollState::CLOSED);
        }

        // if the child is closed, we can drop it
        if is_closed {
            self.remove_child(key).unwrap();
        }

        Ok(())
    }

    /// Remove a child state and all references to it (except timers). Returns `None` if there was
    /// no child with the given key.
    fn remove_child(&mut self, key: ChildTcpKey) -> Option<TcpStateEnum<X>> {
        let entry = self.children.remove(key)?;
        let child = entry.state.unwrap();
        let conn_addrs = entry.conn_addrs;

        // remove the child from any other lists/maps

        remove_from_list(&mut self.accept_queue, &key);
        remove_from_list(&mut self.to_send, &key);
        assert_eq!(self.conn_map.remove(&conn_addrs), Some(key));

        Some(child)
    }

    /// Get the child state.
    fn child(&self, key: ChildTcpKey) -> Option<&TcpStateEnum<X>> {
        self.children.get(key)?.state.as_ref()
    }

    /// Mutate the child's state, and automatically make sure that the parent's state is correctly
    /// synced with the child's state (see [`sync_child`]).
    fn with_child<T>(
        &mut self,
        key: ChildTcpKey,
        f: impl FnOnce(TcpStateEnum<X>) -> (TcpStateEnum<X>, T),
    ) -> Result<T, ChildNotFound> {
        let rv;

        {
            let child = &mut self.children.get_mut(key).ok_or(ChildNotFound)?.state;

            // run the closure
            let mut state = child.take().unwrap();
            (state, rv) = f(state);
            *child = Some(state);
        }

        self.sync_child(key).unwrap();

        Ok(rv)
    }
}

impl<X: Dependencies> TcpStateTrait<X> for ListenState<X> {
    fn close(self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let (new_state, rv) = self.rst_close();
        assert!(rv.is_ok());
        (new_state, Ok(()))
    }

    fn rst_close(mut self) -> (TcpStateEnum<X>, Result<(), RstCloseError>) {
        let child_keys = Vec::from_iter(self.children.keys());

        for key in child_keys {
            self.with_child(key, |child| child.rst_close())
                .unwrap()
                .unwrap();

            // get any packets that it wants to send and add them to our send buffer; removing a
            // packet may cause the child to close which will make `key` invalid, which is why we
            // don't unwrap here
            while let Ok(Ok((header, payload))) = self.with_child(key, |child| child.pop_packet()) {
                assert!(payload.is_empty());
                self.send_buffer.push_back(header);
            }
        }

        // The `rst_close` should have moved the child states to either "closed" or "rst" and
        // possibly queued some RST packets. Then we should have taken those packets from the child
        // state and moved them to our buffer, which would have then moved all child states to
        // "closed". Finally `with_child` would have seen that they closed and removed them from
        // `self.children`.
        assert!(self.children.is_empty());

        // get all rst packets from our send buffer
        let rst_packets: LinkedList<_> = self
            .send_buffer
            .into_iter()
            .filter(|header| header.flags.contains(TcpFlags::RST))
            .collect();

        let new_state = if rst_packets.is_empty() {
            // no RST packets to send, so go directly to the "closed" state
            ClosedState::new(self.common, None, /* was_connected= */ false).into()
        } else {
            RstState::new(self.common, rst_packets, /* was_connected= */ false).into()
        };

        (new_state, Ok(()))
    }

    fn shutdown(self, _how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        // TODO: Linux will reset back to the initial state (allowing future connect(), listen(),
        // etc for the same socket) for SHUT_RD or SHUT_RDWR. But this should probably be handled in
        // a higher layer (for example having the socket replace this tcp state with a new tcp state
        // object).

        (self.into(), Err(ShutdownError::NotConnected))
    }

    fn listen<T, E>(
        mut self,
        backlog: u32,
        associate_fn: impl FnOnce() -> Result<T, E>,
    ) -> (TcpStateEnum<X>, Result<T, ListenError<E>>) {
        // we don't need to associate, but we run this closure anyway; the caller can make this a
        // no-op if it doesn't need to associate
        let rv = match associate_fn() {
            Ok(x) => x,
            Err(e) => return (self.into(), Err(ListenError::FailedAssociation(e))),
        };

        // linux uses a limit of one greater than the provided backlog
        let max_backlog = backlog.saturating_add(1);

        // we're already listening, so must already be associated; just update the backlog
        self.max_backlog = max_backlog;
        (self.into(), Ok(rv))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::IsListening))
    }

    fn accept(mut self) -> (TcpStateEnum<X>, Result<AcceptedTcpState<X>, AcceptError>) {
        let Some(child_key) = self.accept_queue.pop_front() else {
            return (self.into(), Err(AcceptError::NothingToAccept));
        };

        let child = self.remove_child(child_key).unwrap();

        // if the child is in an acceptable state, it's wrapped in an `AcceptedTcpState` and
        // returned to the caller
        let accepted_state = match child.try_into() {
            Ok(x) => x,
            Err(child) => {
                // the child is in a state that we can't return to the caller, so we messed up
                // somewhere earlier
                panic!("Unexpected child TCP state in accept queue: {child:?}");
            }
        };

        (self.into(), Ok(accepted_state))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::NotConnected))
    }

    fn recv(self, _writer: impl Write, _len: usize) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        (self.into(), Err(RecvError::NotConnected))
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // In Linux there is conceptually the syn queue and the accept queue. When the application
        // calls `listen()`, it passes a `backlog` argument. The question is: does this backlog
        // apply to the syn queue, accept queue, or both? Some references[1] and the listen(2)[2]
        // man page say that the backlog only applies to the accept queue, but other blogs[3,4] and
        // stack overflow[5] say that it applies to both queues.
        //
        // The truth is probably more nuanced, and Linux technically doesn't have a "syn queue", but
        // this should be good enough for us. We'll apply the backlog as a limit to both queues
        // (each queue can hold `backlog` entries). In our case, the "syn queue" length is
        // `children.len() - accept_queue.len()`.
        //
        // If the accept queue is full, the application is slow at accept()ing new connections. As a
        // push-back mechanism drop all incoming SYN packets, and incoming ACK packets that are
        // intended for a child in the "syn-received" state (since they would then get added to the
        // accept queue, but the accept queue is full). If the syn queue is full, drop all incoming
        // SYN packets (we don't support SYN cookies). This seems to be along the lines of what
        // Linux does.[4]
        //
        // [1]: https://veithen.io/2014/01/01/how-tcp-backlog-works-in-linux.html
        // [2]: https://man7.org/linux/man-pages/man2/listen.2.html
        // [3]: https://arthurchiao.art/blog/tcp-listen-a-tale-of-two-queues/
        // [4]: https://blog.cloudflare.com/syn-packet-handling-in-the-wild/
        // [5]: https://stackoverflow.com/questions/58183847/

        let max_backlog = self.max_backlog.try_into().unwrap();
        let syn_queue_len = self
            .children
            .len()
            .checked_sub(self.accept_queue.len())
            .unwrap();
        let accept_queue_full = self.accept_queue.len() >= max_backlog;
        let syn_queue_full = syn_queue_len >= max_backlog;

        // if either queue is full, drop all SYN packets
        if header.flags.contains(TcpFlags::SYN) && (accept_queue_full || syn_queue_full) {
            return (self.into(), Ok(0));
        }

        let conn_addrs = RemoteLocalPair::new(header.src(), header.dst());

        // forward the packet to a child state if it's from a known src address
        if let Some(child_key) = self.conn_map.get(&conn_addrs) {
            // if in the "syn-received" state, is an ACK packet, and the accept queue is full, drop
            // the packet
            if matches!(self.child(*child_key), Some(TcpStateEnum::SynReceived(_)))
                && header.flags.contains(TcpFlags::ACK)
                && accept_queue_full
            {
                return (self.into(), Ok(0));
            }

            // forward the packet to the child state
            let rv = self
                .with_child(*child_key, |state| state.push_packet(header, payload))
                .unwrap();

            // propagate any error from the child to the caller
            return (self.into(), rv);
        }

        // this packet is meant for the listener, or for a child that no longer exists

        // drop non-SYN packets
        if !header.flags.contains(TcpFlags::SYN) {
            // it's either for an old child that no longer exists, or is for the listener and
            // doesn't have the SYN flag for some reason
            return (self.into(), Ok(0));
        }

        // we received a SYN packet, so register a new child in the "syn-received" state
        self.register_child(header, payload);

        (self.into(), Ok(0))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        if let Some(header) = self.send_buffer.pop_front() {
            return (self.into(), Ok((header, Payload::default())));
        }

        if let Some(child_key) = self.to_send.pop_front() {
            let rv = self
                .with_child(child_key, |state| state.pop_packet())
                .unwrap();

            // if the child was in the list, then we'll assume it must have a packet to send
            let (header, payload) = rv.unwrap();

            // might as well check this
            debug_assert!(payload.is_empty());

            return (self.into(), Ok((header, payload)));
        }

        (self.into(), Err(PopPacketError::NoPacket))
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::LISTENING;

        if !self.accept_queue.is_empty() {
            poll_state.insert(PollState::READY_TO_ACCEPT);
        }

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        !self.send_buffer.is_empty() || !self.to_send.is_empty()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        None
    }
}

impl<X: Dependencies> SynSentState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        let state = SynSentState { common, connection };

        // if still in the "syn-sent" state after 60 seconds, close it
        let timeout = state.common.current_time() + X::Duration::from_secs(60);
        state.common.register_timer(timeout, |state| {
            if let TcpStateEnum::SynSent(mut state) = state {
                state.common.error = Some(TcpError::TimedOut);

                let (state, rv) = state.rst_close();
                assert!(rv.is_ok());
                state
            } else {
                state
            }
        });

        state
    }
}

impl<X: Dependencies> TcpStateTrait<X> for SynSentState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        // we haven't received a SYN yet, so we can't have received data and don't
        // need to send an RST
        debug_assert!(!self.connection.recv_buf_has_data());

        self.common
            .set_error_if_unset(TcpError::ClosedWhileConnecting);

        let new_state = ClosedState::new(self.common, None, /* was_connected= */ true);
        (new_state.into(), Ok(()))
    }

    fn rst_close(mut self) -> (TcpStateEnum<X>, Result<(), RstCloseError>) {
        // we haven't received a SYN yet, so we can't have received data and don't
        // need to send an RST
        debug_assert!(!self.connection.recv_buf_has_data());

        self.common
            .set_error_if_unset(TcpError::ClosedWhileConnecting);

        let new_state = ClosedState::new(self.common, None, /* was_connected= */ true);
        (new_state.into(), Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we haven't received a SYN yet, so we can't have received data and don't
            // need to send an RST
            debug_assert!(!self.connection.recv_buf_has_data());

            self.common
                .set_error_if_unset(TcpError::ClosedWhileConnecting);

            let new_state = ClosedState::new(self.common, None, /* was_connected= */ true);
            return (new_state.into(), Ok(()));
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::InProgress))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::NotConnected))
    }

    fn recv(self, _writer: impl Write, _len: usize) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        (self.into(), Err(RecvError::NotConnected))
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        // if received SYN and ACK (active open), move to the "established" state
        if self.connection.received_syn() && self.connection.syn_was_acked() {
            let new_state = EstablishedState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        // if received SYN and no ACK (simultaneous open), move to the "syn-received" state
        if self.connection.received_syn() {
            let new_state = SynReceivedState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        // TODO: unsure what to do otherwise; just dropping the packet

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTING;

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> SynReceivedState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        let state = SynReceivedState { common, connection };

        // if still in the "syn-received" state after 60 seconds, close it with a RST
        let timeout = state.common.current_time() + X::Duration::from_secs(60);
        state.common.register_timer(timeout, |state| {
            if let TcpStateEnum::SynReceived(mut state) = state {
                state.common.error = Some(TcpError::TimedOut);

                let (state, rv) = state.rst_close();
                assert!(rv.is_ok());
                return state;
            }

            state
        });

        state
    }
}

impl<X: Dependencies> TcpStateTrait<X> for SynReceivedState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = if self.connection.recv_buf_has_data() {
            // send a RST if there is still data in the receive buffer
            reset_connection(self.common, self.connection).into()
        } else {
            // send a FIN packet
            self.connection.send_fin();

            self.common
                .set_error_if_unset(TcpError::ClosedWhileConnecting);

            // if the connection receives any more data, it should send an RST
            self.connection.send_rst_if_recv_payload();

            FinWaitOneState::new(self.common, self.connection).into()
        };

        (new_state, Ok(()))
    }

    fn rst_close(self) -> (TcpStateEnum<X>, Result<(), RstCloseError>) {
        let new_state = reset_connection(self.common, self.connection);
        (new_state.into(), Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // send a FIN packet
            self.connection.send_fin();

            self.common
                .set_error_if_unset(TcpError::ClosedWhileConnecting);

            let new_state = FinWaitOneState::new(self.common, self.connection);
            return (new_state.into(), Ok(()));
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::InProgress))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::NotConnected))
    }

    fn recv(self, _writer: impl Write, _len: usize) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        (self.into(), Err(RecvError::NotConnected))
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // waiting for the ACK for our SYN

        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        // if received ACK, move to the "established" state
        if self.connection.syn_was_acked() {
            let new_state = EstablishedState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTING;

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> EstablishedState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        EstablishedState { common, connection }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for EstablishedState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = if self.connection.recv_buf_has_data() {
            // send a RST if there is still data in the receive buffer
            reset_connection(self.common, self.connection).into()
        } else {
            // send a FIN packet
            self.connection.send_fin();

            // if the connection receives any more data, it should send an RST
            self.connection.send_rst_if_recv_payload();

            FinWaitOneState::new(self.common, self.connection).into()
        };

        (new_state, Ok(()))
    }

    fn rst_close(self) -> (TcpStateEnum<X>, Result<(), RstCloseError>) {
        let new_state = reset_connection(self.common, self.connection);
        (new_state.into(), Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // send a FIN packet
            self.connection.send_fin();

            let new_state = FinWaitOneState::new(self.common, self.connection);
            return (new_state.into(), Ok(()));
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::AlreadyConnected))
    }

    fn send(
        mut self,
        reader: impl Read,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        let rv = self.connection.send(reader, len);
        (self.into(), rv)
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        let rv = self.connection.recv(writer, len);
        (self.into(), rv)
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        // if received FIN, move to the "close-wait" state
        if self.connection.received_fin() {
            let new_state = CloseWaitState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTED;

        if self.connection.send_buf_has_space() {
            poll_state.insert(PollState::WRITABLE);
        }

        if self.connection.recv_buf_has_data() {
            poll_state.insert(PollState::READABLE);
        }

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> FinWaitOneState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        FinWaitOneState { common, connection }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for FinWaitOneState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = if self.connection.recv_buf_has_data() {
            // send a RST if there is still data in the receive buffer
            reset_connection(self.common, self.connection).into()
        } else {
            // if the connection receives any more data, it should send an RST
            self.connection.send_rst_if_recv_payload();

            // we're already in the process of closing (active close)
            self.into()
        };

        (new_state, Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we're already in the process of closing (active close)
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::AlreadyConnected))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::StreamClosed))
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        let rv = self.connection.recv(writer, len);
        (self.into(), rv)
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        // if received FIN and ACK, move to the "time-wait" state
        if self.connection.received_fin() && self.connection.fin_was_acked() {
            let new_state = TimeWaitState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        // if received FIN, move to the "closing" state
        if self.connection.received_fin() {
            let new_state = ClosingState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        // if received ACK, move to the "fin-wait-two" state
        if self.connection.fin_was_acked() {
            let new_state = FinWaitTwoState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTED;

        if self.connection.recv_buf_has_data() {
            poll_state.insert(PollState::READABLE);
        }

        // we've sent a FIN
        poll_state.insert(PollState::SEND_CLOSED);
        assert!(!poll_state.contains(PollState::WRITABLE));

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> FinWaitTwoState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        FinWaitTwoState { common, connection }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for FinWaitTwoState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = if self.connection.recv_buf_has_data() {
            // send a RST if there is still data in the receive buffer
            reset_connection(self.common, self.connection).into()
        } else {
            // if the connection receives any more data, it should send an RST
            self.connection.send_rst_if_recv_payload();

            // we're already in the process of closing (active close)
            self.into()
        };

        (new_state, Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we're already in the process of closing (active close)
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::AlreadyConnected))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::StreamClosed))
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        let rv = self.connection.recv(writer, len);
        (self.into(), rv)
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        // if received FIN, move to the "time-wait" state
        if self.connection.received_fin() {
            let new_state = TimeWaitState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTED;

        if self.connection.recv_buf_has_data() {
            poll_state.insert(PollState::READABLE);
        }

        // we've sent a FIN
        poll_state.insert(PollState::SEND_CLOSED);
        assert!(!poll_state.contains(PollState::WRITABLE));

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> ClosingState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        ClosingState { common, connection }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for ClosingState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = if self.connection.recv_buf_has_data() {
            // send a RST if there is still data in the receive buffer
            reset_connection(self.common, self.connection).into()
        } else {
            // if the connection receives any more data, it should send an RST
            self.connection.send_rst_if_recv_payload();

            // we're already in the process of closing (active close)
            self.into()
        };

        (new_state, Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we're already in the process of closing (active close)
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::AlreadyConnected))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::StreamClosed))
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        let rv = self.connection.recv(writer, len);

        // the peer won't send any more data (it sent a FIN), so if there's no more data in the
        // buffer, inform the socket
        if matches!(rv, Err(RecvError::Empty)) {
            return (self.into(), Err(RecvError::StreamClosed));
        }

        (self.into(), rv)
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        // if received ACK, move to the "time-wait" state
        if self.connection.fin_was_acked() {
            let new_state = TimeWaitState::new(self.common, self.connection);
            return (new_state.into(), Ok(pushed_len));
        }

        // drop all other packets

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTED;

        // we've received a FIN
        poll_state.insert(PollState::RECV_CLOSED);
        if self.connection.recv_buf_has_data() {
            poll_state.insert(PollState::READABLE);
        }

        // we've sent a FIN
        poll_state.insert(PollState::SEND_CLOSED);
        assert!(!poll_state.contains(PollState::WRITABLE));

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> TimeWaitState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        let state = TimeWaitState { common, connection };

        // taken from /proc/sys/net/ipv4/tcp_fin_timeout
        let timeout = X::Duration::from_secs(60);

        // if still in the "time-wait" state after the timeout, close it
        let timeout = state.common.current_time() + timeout;
        state.common.register_timer(timeout, |state| {
            if let TcpStateEnum::TimeWait(state) = state {
                let recv_buffer = state.connection.into_recv_buffer();
                let new_state =
                    ClosedState::new(state.common, recv_buffer, /* was_connected= */ true);
                new_state.into()
            } else {
                state
            }
        });

        state
    }
}

impl<X: Dependencies> TcpStateTrait<X> for TimeWaitState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        // Linux does not seem to send a RST packet if a "time-wait" socket is closed while having
        // data in the receive buffer, probably because the peer should be in the "closed" state by
        // this point

        // if the connection receives any more data, it should send an RST
        self.connection.send_rst_if_recv_payload();

        // we're already in the process of closing (active close)
        (self.into(), Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we're already in the process of closing (active close)
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::AlreadyConnected))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::StreamClosed))
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        let rv = self.connection.recv(writer, len);

        // the peer won't send any more data (it sent a FIN), so if there's no more data in the
        // buffer, inform the socket
        if matches!(rv, Err(RecvError::Empty)) {
            return (self.into(), Err(RecvError::StreamClosed));
        }

        (self.into(), rv)
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        // TODO: send RST for all packets?
        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTED;

        // we've received a FIN
        poll_state.insert(PollState::RECV_CLOSED);
        if self.connection.recv_buf_has_data() {
            poll_state.insert(PollState::READABLE);
        }

        // we've sent a FIN
        poll_state.insert(PollState::SEND_CLOSED);
        assert!(!poll_state.contains(PollState::WRITABLE));

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> CloseWaitState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        Self { common, connection }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for CloseWaitState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = if self.connection.recv_buf_has_data() {
            // send a RST if there is still data in the receive buffer
            reset_connection(self.common, self.connection).into()
        } else {
            // send a FIN packet
            self.connection.send_fin();

            // if the connection receives any more data, it should send an RST
            self.connection.send_rst_if_recv_payload();

            LastAckState::new(self.common, self.connection).into()
        };

        (new_state, Ok(()))
    }

    fn rst_close(self) -> (TcpStateEnum<X>, Result<(), RstCloseError>) {
        let new_state = reset_connection(self.common, self.connection);
        (new_state.into(), Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // send a FIN packet
            self.connection.send_fin();

            let new_state = LastAckState::new(self.common, self.connection);
            return (new_state.into(), Ok(()));
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::AlreadyConnected))
    }

    fn send(
        mut self,
        reader: impl Read,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        let rv = self.connection.send(reader, len);
        (self.into(), rv)
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        let rv = self.connection.recv(writer, len);

        // the peer won't send any more data (it sent a FIN), so if there's no more data in the
        // buffer, inform the socket
        if matches!(rv, Err(RecvError::Empty)) {
            return (self.into(), Err(RecvError::StreamClosed));
        }

        (self.into(), rv)
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTED;

        if self.connection.send_buf_has_space() {
            poll_state.insert(PollState::WRITABLE);
        }

        // we've received a FIN
        poll_state.insert(PollState::RECV_CLOSED);
        if self.connection.recv_buf_has_data() {
            poll_state.insert(PollState::READABLE);
        }

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> LastAckState<X> {
    fn new(common: Common<X>, connection: Connection<X::Instant>) -> Self {
        Self { common, connection }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for LastAckState<X> {
    fn close(mut self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        let new_state = if self.connection.recv_buf_has_data() {
            // send a RST if there is still data in the receive buffer
            reset_connection(self.common, self.connection).into()
        } else {
            // if the connection receives any more data, it should send an RST
            self.connection.send_rst_if_recv_payload();

            // we're already in the process of closing (passive close)
            self.into()
        };

        (new_state, Ok(()))
    }

    fn shutdown(mut self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if how == Shutdown::Read || how == Shutdown::Both {
            self.connection.send_rst_if_recv_payload()
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we're already in the process of closing (passive close)
        }

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::AlreadyConnected))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::StreamClosed))
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        let rv = self.connection.recv(writer, len);

        // the peer won't send any more data (it sent a FIN), so if there's no more data in the
        // buffer, inform the socket
        if matches!(rv, Err(RecvError::Empty)) {
            return (self.into(), Err(RecvError::StreamClosed));
        }

        (self.into(), rv)
    }

    fn push_packet(
        mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // make sure that the packet src/dst addresses are valid for this connection
        if !self.connection.packet_addrs_match(header) {
            // must drop the packet
            return (self.into(), Ok(0));
        }

        let pushed_len = match self.connection.push_packet(header, payload) {
            Ok(v) => v,
            Err(e) => return (self.into(), Err(e)),
        };

        // if the connection was reset
        if self.connection.is_reset() {
            if header.flags.contains(TcpFlags::RST) {
                self.common.set_error_if_unset(TcpError::ResetReceived);
            }

            let new_state = connection_was_reset(self.common, self.connection);
            return (new_state, Ok(pushed_len));
        }

        // if received ACK, move to the "closed" state
        if self.connection.fin_was_acked() {
            let recv_buffer = self.connection.into_recv_buffer();
            let new_state =
                ClosedState::new(self.common, recv_buffer, /* was_connected= */ true);
            return (new_state.into(), Ok(pushed_len));
        }

        (self.into(), Ok(pushed_len))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        let rv = self.connection.pop_packet(self.common.current_time());
        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CONNECTED;

        // we've received a FIN
        poll_state.insert(PollState::RECV_CLOSED);
        if self.connection.recv_buf_has_data() {
            poll_state.insert(PollState::READABLE);
        }

        // we've sent a FIN
        poll_state.insert(PollState::SEND_CLOSED);
        assert!(!poll_state.contains(PollState::WRITABLE));

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        self.connection.wants_to_send()
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        Some((self.connection.local_addr, self.connection.remote_addr))
    }
}

impl<X: Dependencies> RstState<X> {
    /// All packets must contain `TcpFlags::RST`.
    fn new(common: Common<X>, rst_packets: LinkedList<TcpHeader>, was_connected: bool) -> Self {
        debug_assert!(rst_packets.iter().all(|x| x.flags.contains(TcpFlags::RST)));
        assert!(!rst_packets.is_empty());

        Self {
            common,
            send_buffer: rst_packets,
            was_connected,
        }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for RstState<X> {
    fn close(self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        // we're already in the process of closing; do nothing
        (self.into(), Ok(()))
    }

    fn shutdown(self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if !self.was_connected {
            return (self.into(), Err(ShutdownError::NotConnected));
        }

        if how == Shutdown::Read || how == Shutdown::Both {
            // we've been reset, so nothing to do
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we're already in the process of closing; do nothing
        }

        // TODO: should we return an error or do nothing?

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        if self.was_connected {
            (self.into(), Err(ConnectError::AlreadyConnected))
        } else {
            (self.into(), Err(ConnectError::InvalidState))
        }
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        if self.was_connected {
            (self.into(), Err(SendError::StreamClosed))
        } else {
            (self.into(), Err(SendError::NotConnected))
        }
    }

    fn recv(self, _writer: impl Write, _len: usize) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        if self.was_connected {
            (self.into(), Err(RecvError::StreamClosed))
        } else {
            (self.into(), Err(RecvError::NotConnected))
        }
    }

    fn push_packet(
        self,
        _header: &TcpHeader,
        _payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        // do nothing; drop all packets received in this state
        (self.into(), Ok(0))
    }

    fn pop_packet(
        mut self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        // if we're in this state we must have a packet queued
        let header = self.send_buffer.pop_front().unwrap();
        let packet = (header, Payload::default());

        // we're only supposed to send RST packets in this state
        assert!(packet.0.flags.contains(TcpFlags::RST));

        // if we have no more packets to send
        if self.send_buffer.is_empty() {
            let new_state = ClosedState::new(
                self.common,
                None,
                /* was_connected= */ self.was_connected,
            );
            return (new_state.into(), Ok(packet));
        }

        (self.into(), Ok(packet))
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::RECV_CLOSED | PollState::SEND_CLOSED;

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        if self.was_connected {
            poll_state.insert(PollState::CONNECTED);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        // if we're in this state we must have a packet queued
        assert!(!self.send_buffer.is_empty());
        true
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        None
    }
}

impl<X: Dependencies> ClosedState<X> {
    fn new(common: Common<X>, recv_buffer: Option<RecvQueue>, was_connected: bool) -> Self {
        let recv_buffer = recv_buffer.unwrap_or_else(|| RecvQueue::new(Seq::new(0)));

        if !was_connected {
            assert!(recv_buffer.is_empty());
        }

        Self {
            common,
            recv_buffer,
            was_connected,
        }
    }
}

impl<X: Dependencies> TcpStateTrait<X> for ClosedState<X> {
    fn close(self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        // already closed; do nothing
        (self.into(), Ok(()))
    }

    fn shutdown(self, how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        if !self.was_connected {
            return (self.into(), Err(ShutdownError::NotConnected));
        }

        if how == Shutdown::Read || how == Shutdown::Both {
            // we've been reset, so nothing to do
        }

        if how == Shutdown::Write || how == Shutdown::Both {
            // we're already in the process of closing; do nothing
        }

        // TODO: should we return an error or do nothing?

        (self.into(), Ok(()))
    }

    fn connect<T, E>(
        self,
        _remote_addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        if self.was_connected {
            (self.into(), Err(ConnectError::AlreadyConnected))
        } else {
            (self.into(), Err(ConnectError::InvalidState))
        }
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        if !self.was_connected {
            return (self.into(), Err(SendError::NotConnected));
        }

        (self.into(), Err(SendError::StreamClosed))
    }

    fn recv(
        mut self,
        writer: impl Write,
        len: usize,
    ) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        if !self.was_connected {
            return (self.into(), Err(RecvError::NotConnected));
        }

        if self.recv_buffer.is_empty() {
            return (self.into(), Err(RecvError::StreamClosed));
        }

        let rv = self.recv_buffer.read(writer, len).map_err(RecvError::Io);

        (self.into(), rv)
    }

    fn clear_error(&mut self) -> Option<TcpError> {
        self.common.error.take()
    }

    fn poll(&self) -> PollState {
        let mut poll_state = PollState::CLOSED;

        poll_state.insert(PollState::RECV_CLOSED);
        if !self.recv_buffer.is_empty() {
            poll_state.insert(PollState::READABLE);
        }

        poll_state.insert(PollState::SEND_CLOSED);
        assert!(!poll_state.contains(PollState::WRITABLE));

        if self.was_connected {
            poll_state.insert(PollState::CONNECTED);
        }

        if self.common.error.is_some() {
            poll_state.insert(PollState::ERROR);
        }

        poll_state
    }

    fn wants_to_send(&self) -> bool {
        false
    }

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        None
    }
}

/// Reset the connection, get the resulting RST packet, and return a new `RstState` that will send
/// this RST packet.
fn reset_connection<X: Dependencies>(
    common: Common<X>,
    mut connection: Connection<X::Instant>,
) -> RstState<X> {
    connection.send_rst();

    let new_state = connection_was_reset(common, connection);

    let TcpStateEnum::Rst(new_state) = new_state else {
        panic!("We called `send_rst()` above but aren't now in the \"rst\" state: {new_state:?}");
    };

    new_state
}

/// For a connection that was reset (either by us or by the peer), check if it has a remaining RST
/// packet to send, and return a new `RstState` that will send this RST packet or a new
/// `ClosedState` if not.
fn connection_was_reset<X: Dependencies>(
    mut common: Common<X>,
    mut connection: Connection<X::Instant>,
) -> TcpStateEnum<X> {
    assert!(connection.is_reset());

    let now = common.current_time();

    // check if there's an RST packet to send
    if let Ok((header, payload)) = connection.pop_packet(now) {
        assert!(payload.is_empty());
        debug_assert!(connection.pop_packet(now).is_err());

        common.set_error_if_unset(TcpError::ResetSent);

        let rst_packets = [header].into_iter().collect();
        RstState::new(common, rst_packets, /* was_connected= */ true).into()
    } else {
        // the receive buffer is cleared when a connection is reset, which is why we pass `None`
        ClosedState::new(common, None, /* was_connected= */ true).into()
    }
}
