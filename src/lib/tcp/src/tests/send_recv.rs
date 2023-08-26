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
