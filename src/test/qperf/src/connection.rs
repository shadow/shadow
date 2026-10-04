//! Connection manager for a QUIC-based client/server performance tester.
//
//! This module provides a thin, async-friendly wrapper around **quiche** that
//! drives a single QUIC connection over a `smol::net::UdpSocket`. It handles the
//! UDP I/O loop, buffers, and timeout scheduling while exposing a high-level
//! `transfer` API that can be called to move a fixed-size payload while
//! recording standard QUIC connection metrics.
//!
//! The implementation is deliberately minimal - it only supports a single
//! client-initiated stream (via `StreamManager`) and does not implement
//! connection retries or server-initiated streams.

use std::net::SocketAddr;

use futures::future::{Either, FutureExt};
use quiche::{Config, Connection, ConnectionId, RecvInfo, SendInfo};
use rand::RngExt;
use smol::Timer;
use smol::net::UdpSocket;

use crate::config;
use crate::stream::StreamManager;

/// Default capacity for our send and receive buffers.
const BUF_CAPACITY: usize = 65_535; // 64 KiB

/// Manages a single QUIC connection over UDP.
///
/// # Typical workflow
/// 1. Call [`Self::connect`] (client) or [`Self::listen`] (server) to
///    obtain an instance.
/// 2. Invoke [`Self::into_transfer`] with the desired payload length; the
///    method drives the event loop until the stream and connection have both
///    closed.
pub struct ConnectionManager {
    /// The underlying [`smol::net::UdpSocket`] used for all network I/O.
    sock: UdpSocket,
    /// The [`quiche::Connection`] instance representing the QUIC state.
    conn: Connection,
    /// Static, fixed-size buffer to hold inbound UDP packets until we transfer
    /// the data to QUIC.
    recv_buf: Vec<u8>,
    /// Static, fixed-size buffer to hold outbound UDP packets from QUIC until
    /// we transfer them to the kernel/network.
    send_buf: Vec<u8>,
    /// If we have a UDP packet that needs to be sent out, this holds the length
    /// of the packet within our `send_buf` and the [`quiche::SendInfo`] from
    /// QUIC. (The range of bytes that make up the packet in [`Self::send_buf`]
    /// is [0..length).)
    next_send: Option<(usize, SendInfo)>,
}

/// Internal event that drives the async I/O loop.
///
/// The enum is used by [`ConnectionManager::next`] to report which of multiple
/// concurrent asynchronous events completed first.
enum Event {
    /// A UDP packet was successfully transmitted. The contained value is the
    /// number of bytes written via [`smol::net::UdpSocket::send_to`].
    Sent(usize),
    /// A UDP packet arrived from the network. The contained tuple holds the
    /// length of data read via [`smol::net::UdpSocket::recv_from`] and the
    /// address of the peer from which we received the packet.
    Received((usize, SocketAddr)),
    /// QUIC reported that a timeout elapsed and
    /// [`quiche::Connection::on_timeout`] should be invoked.
    TimedOut,
}

impl ConnectionManager {
    /// Creates a new client-side QUIC connection.
    ///
    /// The function binds a [`smol::net::UdpSocket`] and initiates the QUIC
    /// client connection via [`quiche::connect`].
    ///
    /// # Parameters
    /// * `local` - The local address to bind the UDP socket to. `0.0.0.0:0` can
    ///   be used to let the OS choose a port.
    /// * `peer` - The remote socket address of the QUIC server.
    /// * `config` - A mutable [`quiche::Config`] to apply when connecting.
    ///
    /// # Returns
    /// A fully initialized [`ConnectionManager`] ready to drive the I/O loop. Any
    /// underlying socket or QUIC error is propagated.
    pub async fn connect(
        local: SocketAddr,
        peer: SocketAddr,
        mut config: Config,
    ) -> anyhow::Result<Self> {
        // Get our underlying udp socket.
        let sock = UdpSocket::bind(local).await?;

        // Use the OS-assigned address:port for our QUIC connection.
        let local = sock.local_addr()?;

        // Quic requires a connection id.
        let cid = generate_connection_id();

        // Establish a connection context for the peer. The first param is None
        // so that we don't validate the peer's cert.
        let mut conn = quiche::connect(None, &cid, local, peer, &mut config)?;

        // Record performance metrics the connection.
        config::enable_quic_qlog(&mut conn, "client")?;

        let (recv_buf, mut send_buf) = (vec![0u8; BUF_CAPACITY], vec![0u8; BUF_CAPACITY]);

        // Queue the initial QUIC connection handshake message.
        let (len, info) = conn.send(&mut send_buf)?;

        Ok(Self {
            sock,
            conn,
            recv_buf,
            send_buf,
            next_send: Some((len, info)),
        })
    }

    /// Creates a new server-side QUIC connection.
    ///
    /// The function blocks until it receives the first packet from a client,
    /// then builds the server connection state around it.
    ///
    /// # Parameters
    /// * `local` - The address to bind the listening [`smol::net::UdpSocket`].
    /// * `config` - A mutable [`quiche::Config`] to apply to the accepted connection.
    ///
    /// # Returns
    /// A [`ConnectionManager`] ready to drive the I/O loop. Any underlying socket
    /// or QUIC error is propagated.
    pub async fn listen(local: SocketAddr, mut config: Config) -> anyhow::Result<Self> {
        // Get our underlying udp socket.
        let sock = UdpSocket::bind(local).await?;

        // Use the OS-assigned address:port for our QUIC connection.
        let local = sock.local_addr()?;

        let (mut recv_buf, send_buf) = (vec![0u8; BUF_CAPACITY], vec![0u8; BUF_CAPACITY]);

        // Wait for the initial handshake message.
        let (len, from) = sock.recv_from(&mut recv_buf).await?;

        // Quic requires a connection id.
        let cid = generate_connection_id();

        // Establish a connection context for the server. The None param is for
        // retrying a connection, which we do not support.
        let mut conn = quiche::accept(&cid, None, local, from, &mut config)?;

        // Record performance metrics the connection.
        config::enable_quic_qlog(&mut conn, "server")?;

        let mut mgr = Self {
            sock,
            conn,
            recv_buf,
            send_buf,
            next_send: None,
        };

        // Push the data from the sock.recv_from() into the QUIC conn.
        mgr.drain_recv_buf(len, from)?;

        Ok(mgr)
    }

    /// Drives the full data transfer for a single stream.
    ///
    /// The method creates a [`StreamManager`] with the requested payload size and
    /// then runs the main transfer I/O loop which repeatedly:
    ///
    /// 1. Calls [`StreamManager.process()`] to let the stream feed fake stream
    ///    data into the connection, generating QUIC frames.
    /// 2. Moves any pending outbound data from QUIC into our UDP send buffer.
    /// 3. Awaits the next multiplexed network or timeout event.
    /// 4. Handles the event that occured next.
    ///
    /// The loop exits when both the stream reports [`StreamManager::finished`]
    /// **and** the QUIC connection has entered the closed state
    /// ([`quiche::Connection::is_closed`]). Any I/O or QUIC error is
    /// propagated.
    pub async fn into_transfer(&mut self, send_len: usize) -> anyhow::Result<()> {
        // Create a stream to generate the QUIC payload data.
        let mut stream = if self.conn.is_server() {
            StreamManager::new_server(send_len)
        } else {
            StreamManager::new_client(send_len)
        };

        loop {
            // Transfer all possible stream data.
            stream.process(&mut self.conn)?;

            // Get messages QUIC wants to send to the network into our buf.
            self.fill_send_buf()?;

            // Handle whichever event occurrs next.
            let event = self.next().await?;
            self.handle(event)?;

            // If all data is sent and the idle timeout causes a close.
            if stream.finished() && self.conn.is_closed() {
                return Ok(());
            }
        }
    }

    /// Pulls pending UDP data from the QUIC connection into [`Self::send_buf`].
    ///
    /// If [`Self::next_send`] is [`Some`], we already have UDP data that needs
    /// to be sent to the network and this function does nothing. If
    /// [`Self::next_send`] is [`None`], our [`Self::send_buf`] is clear and we
    /// ask QUIC to add to it, cacheing the corresponding length and
    /// [`quiche::SendInfo`] in [`Self::next_send`] to inform our
    /// [`smol::net::UdpSocket::send_to`] call later.
    fn fill_send_buf(&mut self) -> anyhow::Result<()> {
        if self.next_send.is_none() {
            match self.conn.send(&mut self.send_buf) {
                Ok((len, info)) => self.next_send = Some((len, info)),
                Err(quiche::Error::Done) => {} // Valid, nothing to send for now.
                Err(e) => return Err(e.into()),
            }
        }
        Ok(())
    }

    /// Repeatedly pushes UDP data in our [`Self::recv_buf`] into the QUIC connection
    /// until all `len` bytes have been consumed or QUIC returns an error. After
    /// this function is called, our [`Self::recv_buf`] is considered "cleared".
    fn drain_recv_buf(&mut self, len: usize, from: SocketAddr) -> anyhow::Result<()> {
        let to = self.sock.local_addr()?;
        let info = RecvInfo { from, to };
        let mut cursor = 0;

        // This needs to be done in a loop, because QUIC may not process all of
        // the data at once.
        while cursor < len {
            match self.conn.recv(&mut self.recv_buf[cursor..len], info) {
                Ok(n) => cursor += n,
                Err(e) => return Err(e.into()),
            }
        }
        Ok(())
    }

    /// Waits for the next I/O or timeout event and returns an [`Event`].
    ///
    /// The function constructs three futures, one for sending from the
    /// [`Self::send_buf`] to the network, one for receiving from the network
    /// into the [`Self::recv_buf`], and one for next QUIC timeout. Whichever
    /// event occurs first is returned.
    async fn next(&mut self) -> anyhow::Result<Event> {
        // Get the send future, using a dummy if we have nothing to send.
        let send_fut = if let Some((len, info)) = self.next_send {
            Either::Left(self.sock.send_to(&self.send_buf[..len], info.to))
        } else {
            Either::Right(smol::future::pending::<std::io::Result<usize>>())
        }
        .fuse();

        // Get the receive future.
        let recv_fut = self.sock.recv_from(&mut self.recv_buf).fuse();

        // Get a future for the next time QUIC wants a timeout.
        let timeout_fut = match self.conn.timeout() {
            Some(dur) => Timer::after(dur),
            None => Timer::never(),
        }
        .fuse();

        // Monitor all branches simultaneously.
        futures::pin_mut!(send_fut, recv_fut, timeout_fut);
        Ok(futures::select! {
            result = send_fut => Event::Sent(result?),
            result = recv_fut => Event::Received(result?),
            _ = timeout_fut => Event::TimedOut,
        })
    }

    /// Processes a single `Event` produced by [`Self::next`].
    ///
    /// If a message was sent, we "clear" [`Self::send_buf`], if data was
    /// received, we pass all of it to the QUIC connection via
    /// [`Self::drain_recv_buf`], and we notify QUIC if the timeout occurred.
    fn handle(&mut self, event: Event) -> anyhow::Result<()> {
        match event {
            Event::Sent(n) => {
                let (len, info) = self.next_send.take().unwrap();
                assert_eq!(n, len);
                log::trace!("Sent {len} bytes to {:?}", info.to);
            }
            Event::Received((len, from)) => {
                log::trace!("Received {len} bytes from {:?}", from);
                self.drain_recv_buf(len, from)?;
            }
            Event::TimedOut => self.conn.on_timeout(),
        };
        Ok(())
    }
}

/// Generates a new random QUIC connection identifier.
///
/// The function creates a 16-byte identifier using the global random number
/// generator from the **rand** crate and returns it with a `'static` lifetime
/// (the owned `Vec<u8>` is stored inside the `ConnectionId` instance).
///
/// This identifier is used both on the client side ([`quiche::connect`]) and
/// the server side ([`quiche::accept`]) to satisfy QUIC's requirement for a
/// connection ID.
fn generate_connection_id() -> ConnectionId<'static> {
    let mut raw_id = [0u8; 16];
    rand::rng().fill(&mut raw_id);
    ConnectionId::from_vec(raw_id.to_vec())
}
