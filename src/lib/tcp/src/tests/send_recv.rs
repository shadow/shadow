//! Test sending and receiving.

use std::cell::{Ref, RefCell};
use std::rc::Rc;

use bytes::Bytes;

use crate::tests::{establish_helper, Host, Scheduler, TcpSocket, TestEnvState};
use crate::{Ipv4Header, TcpFlags, TcpHeader, TcpState};

#[test]
fn test_send_recv() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // get an established tcp socket
    let tcp = establish_helper(&scheduler, &mut host);

    // send on the socket
    TcpSocket::sendmsg(&tcp, &b"hello"[..], 5).unwrap();

    // check the packet sent by the socket
    let (_, payload) = scheduler.pop_packet().unwrap();
    assert_eq!(payload[..], b"hello"[..]);

    // send two packets to the socket
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::empty(),
        src_port: 20,
        dst_port: 10,
        seq: 1,
        ack: 6,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut()
        .push_in_packet(&header, Bytes::from(&b"hello"[..]));
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::empty(),
        src_port: 20,
        dst_port: 10,
        seq: 6,
        ack: 6,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut()
        .push_in_packet(&header, Bytes::from(&b"world"[..]));

    // recv on the socket
    let mut recv_buf = vec![0; 10];
    TcpSocket::recvmsg(&tcp, &mut recv_buf[..], 10).unwrap();
    assert_eq!(recv_buf, b"helloworld");
}

/// This test tries to make sure that an acknowledgement sent while the socket's usable send window
/// (send window excluding in-flight not-acked data) is empty uses the correct sequence number.
/// (This test doesn't require that the usable send window is actually empty, just that it's empty
/// enough that TCP decides it can't/shouldn't send more payload packets.)
#[test]
// this test causes miri to time-out
#[cfg_attr(miri, ignore)]
fn test_ack_with_empty_usable_send_window() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // get an established tcp socket
    let tcp = establish_helper(&scheduler, &mut host);

    // send on the socket until the send buffer has more data than the usable send window allows
    let max_in_flight = s(&tcp)
        .as_established()
        .unwrap()
        .connection
        .send
        .window_range()
        .len();

    let mut buffered = 0;
    while buffered <= max_in_flight as usize {
        buffered += TcpSocket::sendmsg(&tcp, &b"hello"[..], 5).unwrap();
    }

    // read all of the packets it sent and make sure the sequence number is consistent
    let mut next_seq = None;
    while let Some((header, payload)) = scheduler.pop_packet() {
        if next_seq.is_none() {
            next_seq = Some(header.seq as usize);
        }

        let next_seq = next_seq.as_mut().unwrap();
        assert_eq!(*next_seq, header.seq as usize);
        *next_seq += payload.len();
    }

    // send a packet with a payload to trigger an acknowledgement
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::empty(),
        src_port: 20,
        dst_port: 10,
        seq: 1,
        ack: 1,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut()
        .push_in_packet(&header, Bytes::from(&b"world"[..]));

    // check the packet sent by the socket
    let (header, payload) = scheduler.pop_packet().unwrap();

    // should have acked the packet we sent above using the correct sequence number
    assert!(header.flags.contains(TcpFlags::ACK));
    assert_eq!(header.ack, 6);
    assert_eq!(header.seq as usize, next_seq.unwrap());

    // we haven't acked any of the data it sent, so its usable send window should still be empty and
    // should not have sent any data
    assert!(payload.is_empty());
}
