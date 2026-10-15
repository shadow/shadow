//! Stream manager for a QUIC-based client/server performance tester.
//!
//! This module implements a tiny state machine that drives a single QUIC
//! stream (client-initiated or server-discovered) through the phases required
//! to complete a QUIC-based payload transfer:
//!
//! 1. Handshake - wait for the QUIC connection to be established.
//! 2. Identify - determine the stream identifier.
//! 3. Read/Write - exchange payload data.
//! 4. Close - ensure the stream is finished.
//! 5. Finished - all incoming stream data has been, and outgoing data pushed to
//!    the QUIC connection buffers.

use quiche::Connection;

const CHUNK_SIZE: usize = 8192;

/// Defines the progress of the stream transfer process
/// (`Handshake -> Identify -> Read/Write -> Close -> Finished`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StreamState {
    /// Connection is still being established.
    Handshake,
    /// Stream identifier is being negotiated (client already knows it, server
    /// discovers it from the first readable stream).
    Identify,
    /// Both reading and writing are enabled.
    ReadWrite,
    /// Writing has completed and we are only reading data from the peer.
    Read,
    /// Reading has completed and we are only writing data to the peer.
    Write,
    /// Waiting for verify that the QUIC connection considers our stream closed.
    Close,
    /// All stream I/O is complete and the stream is closed.
    Finished,
}

/// A manager used to drive a single QUIC stream for a data-transfer test.
pub struct StreamManager {
    /// The QUIC stream identifier. `None` for a server until the first readable
    /// stream is observed.
    stream_id: Option<u64>,
    /// Temporary static, fixed-size buffer used for both stream reads and writes.
    buf: Vec<u8>,
    /// Bytes left to send to the peer.
    remaining: usize,
    /// Current state in our state machine.
    state: StreamState,
}

impl StreamManager {
    /// Create a client-side manager with a predefined stream ID (`0`).
    ///
    /// The client knows the stream identifier from the outset, so the manager
    /// is constructed with `stream_id = Some(0)`.  The caller supplies the
    /// total number of payload bytes to be transmitted.
    pub fn new_client(num_bytes: usize) -> Self {
        Self::new(num_bytes, Some(0))
    }

    /// Create a server-side manager that will discover the stream ID.
    ///
    /// The server starts with `stream_id = None` and will set it once a
    /// readable stream appears on the connection.
    pub fn new_server(num_bytes: usize) -> Self {
        Self::new(num_bytes, None)
    }

    /// Core constructor shared by the client and server helpers. Allocates a
    /// fixed-size buffer and initializes the state machine to
    /// [`StreamState::Handshake`].
    fn new(num_bytes: usize, stream_id: Option<u64>) -> Self {
        Self {
            stream_id,
            buf: vec![0u8; CHUNK_SIZE],
            remaining: num_bytes,
            state: StreamState::Handshake,
        }
    }

    /// Returns `true` iff the manager has reached [`StreamState::Finished`].
    pub fn finished(&self) -> bool {
        self.state == StreamState::Finished
    }

    /// Transition to a new `StreamState` and emit a log line.
    ///
    /// This helper centralises state changes so that the transition is always
    /// recorded with `log::info!`.
    fn set_state(&mut self, state: StreamState) {
        log::info!("Transitioning from {:?} to {:?}", self.state, state);
        self.state = state;
    }

    /// Processes all incoming and outgoing application data for the stream.
    /// Returns the updated `StreamStatus`.
    ///
    /// Drive the stream forward by handling all pending reads and writes.
    ///
    /// The method should be called repeatedly by the I/O main loop. It examines
    /// [`Self::state`], interacts with the provided [`quiche::Connection`], and
    /// returns the **new** [`StreamState`] or an error propagated from the QUIC
    /// library.
    ///
    /// The state-machine logic is:
    ///
    /// 1. **Handshake -> Identify** - once the connection is established.
    /// 2. **Identify -> ReadWrite** - client already knows the ID; server gets
    ///    it from the first readable stream on the connection.
    /// 3. **ReadWrite / Read** - pull inbound stream data from the connection.
    ///    When we read a `fin`, transition to [`StreamState::Close`], or
    ///    [`StreamState::Write`] if we have yet to finish writing all stream
    ///    data.
    /// 4. **ReadWrite / Write** - push outbound stream data to the connection,
    ///    respecting QUIC flow- and congestion-control. When all bytes and the
    ///    `fin` are sent, transition to [`StreamState::Close`], or
    ///    [`StreamState::Read`] if we have yet to finish reading all stream
    ///    data.
    /// 5. **Close** - transition to [`StreamState::Finished`] when quiche
    ///    indicates the stream is finished and closed.
    pub fn process(&mut self, conn: &mut Connection) -> Result<StreamState, quiche::Error> {
        if self.state == StreamState::Handshake && conn.is_established() {
            self.set_state(StreamState::Identify);
        }

        if self.state == StreamState::Identify {
            // Client already has the id, server discovers it.
            if let Some(sid) = self.stream_id {
                log::info!("Client initiating stream {sid}");
                self.set_state(StreamState::ReadWrite);
            } else if let Some(sid) = conn.readable().next() {
                log::info!("Server discovered new stream {sid}");
                self.stream_id = Some(sid);
                self.set_state(StreamState::ReadWrite);
            }
        }

        // Read loop to drain the connection of all stream payload data.
        if matches!(self.state, StreamState::ReadWrite | StreamState::Read) {
            let sid = *self.stream_id.as_ref().unwrap();

            while let Ok((len, fin)) = conn.stream_recv(sid, &mut self.buf) {
                log::debug!("Stream {sid} dequeued {len} bytes. fin={fin}");

                if fin {
                    // Reading is done.
                    let next = match self.state {
                        StreamState::ReadWrite => StreamState::Write,
                        StreamState::Read => StreamState::Close,
                        _ => unreachable!(),
                    };
                    self.set_state(next);
                }
            }
        }

        // Write stream data as fast as window flow-control allows.
        if matches!(self.state, StreamState::ReadWrite | StreamState::Write) {
            let sid = *self.stream_id.as_ref().unwrap();

            while self.remaining > 0 {
                let to_write = std::cmp::min(self.remaining, CHUNK_SIZE);
                let fin = self.remaining == to_write;

                match conn.stream_send(sid, &self.buf[..to_write], fin) {
                    Ok(len) => {
                        self.remaining = self.remaining.saturating_sub(len);
                        log::debug!(
                            "Stream {sid} enqueued {len} bytes. Remaining: {}, fin={fin}",
                            self.remaining
                        );

                        if self.remaining == 0 {
                            // Writing is done.
                            let next = match self.state {
                                StreamState::ReadWrite => StreamState::Read,
                                StreamState::Write => StreamState::Close,
                                _ => unreachable!(),
                            };
                            self.set_state(next);
                        }
                    }
                    Err(quiche::Error::Done) => break,
                    Err(e) => return Err(e),
                }
            }
        }

        // If we are in the `Close` state, we should have already gotten a read
        // fin, and should have indicated to quiche of our write fin. So
        // effectively, quiche likely already considers us closed.
        // TODO: maybe we can remove the `Close` state, and this block.
        if self.state == StreamState::Close {
            let sid = *self.stream_id.as_ref().unwrap();
            if conn.stream_finished(sid) && conn.stream_closed(sid) {
                self.set_state(StreamState::Finished);
            }
        }

        Ok(self.state)
    }
}
