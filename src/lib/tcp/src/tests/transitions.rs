//! Test operations that are expected to result in state transitions.

use std::cell::{Ref, RefCell};
use std::rc::Rc;

use crate::tests::util::time::Duration;
use crate::tests::{Errno, Host, Scheduler, TcpSocket, TestEnvState, establish_helper};
use crate::{Ipv4Header, Payload, TcpConfig, TcpFlags, TcpHeader, TcpState};

#[test]
fn test_close() {
    let scheduler = Scheduler::new();

    let tcp = TcpSocket::new(&scheduler, TcpConfig::default());
    let mut tcp_ref = tcp.borrow_mut();
    assert!(tcp_ref.tcp_state().as_init().is_some());

    tcp_ref.close().unwrap();
    assert!(tcp_ref.tcp_state().as_closed().is_some());
}

#[test]
fn test_listen() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    let tcp = TcpSocket::new(&scheduler, TcpConfig::default());
    assert!(tcp.borrow().tcp_state().as_init().is_some());

    TcpSocket::listen(&tcp, &mut host, 10).unwrap();
    assert_eq!(
        tcp.borrow().tcp_state().as_listen().unwrap().max_backlog,
        11
    );

    // we can update the backlog while already in the "listen" state
    TcpSocket::listen(&tcp, &mut host, 2).unwrap();
    assert_eq!(tcp.borrow().tcp_state().as_listen().unwrap().max_backlog, 3);
}

#[test]
fn test_accept() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    let tcp = TcpSocket::new(&scheduler, TcpConfig::default());
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::listen(&tcp, &mut host, 10).unwrap();
    assert_eq!(s(&tcp).as_listen().unwrap().max_backlog, 11);
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 0);

    // send the SYN
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::SYN,
        src_port: 10,
        dst_port: 20,
        seq: 0,
        ack: 0,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // read the SYN+ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN | TcpFlags::ACK);

    // the connection is not yet established so the accept() fails
    assert_eq!(
        tcp.borrow_mut().accept(&mut host).err(),
        Some(Errno::EAGAIN)
    );

    // send the ACK
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::ACK,
        src_port: 10,
        dst_port: 20,
        seq: 1,
        ack: 1,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // the connection is now established
    let accepted_socket = tcp.borrow_mut().accept(&mut host).unwrap();
    assert!(s(&accepted_socket).as_established().is_some());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 0);
}

/// Test accept()ing a child socket that is in the "close-wait" state (has already received a FIN).
#[test]
fn test_accept_close_wait() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    let tcp = TcpSocket::new(&scheduler, TcpConfig::default());
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::listen(&tcp, &mut host, 10).unwrap();
    assert_eq!(s(&tcp).as_listen().unwrap().max_backlog, 11);
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 0);

    // send the SYN
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::SYN,
        src_port: 10,
        dst_port: 20,
        seq: 0,
        ack: 0,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // read the SYN+ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN | TcpFlags::ACK);

    // send the ACK to move the child to the "established" state
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::ACK,
        src_port: 10,
        dst_port: 20,
        seq: 1,
        ack: 1,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // send a FIN to move the child to the "close-wait" state
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::FIN,
        src_port: 10,
        dst_port: 20,
        seq: 1,
        ack: 1,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);

    // accept the new socket, which should be in the "close-wait" state
    let accepted_socket = tcp.borrow_mut().accept(&mut host).unwrap();
    assert!(s(&accepted_socket).as_close_wait().is_some());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 0);
}

#[test]
fn test_connect_active_open() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    let tcp = TcpSocket::new(&scheduler, TcpConfig::default());
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send the SYN+ACK
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: *tcp_bind_addr.ip(),
        },
        flags: TcpFlags::SYN | TcpFlags::ACK,
        src_port: 10,
        dst_port: tcp_bind_addr.port(),
        seq: 0,
        ack: 1,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);
}

#[test]
fn test_connect_simultaneous_open() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    let tcp = TcpSocket::new(&scheduler, TcpConfig::default());
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send a SYN
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: *tcp_bind_addr.ip(),
        },
        flags: TcpFlags::SYN,
        src_port: 10,
        dst_port: tcp_bind_addr.port(),
        seq: 0,
        ack: 0,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_syn_received().is_some());

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);

    // send an ACK
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: *tcp_bind_addr.ip(),
        },
        flags: TcpFlags::ACK,
        src_port: 10,
        dst_port: tcp_bind_addr.port(),
        seq: 1,
        ack: 1,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());
}

#[test]
fn test_passive_close() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // get an established tcp socket
    let tcp = establish_helper(&scheduler, &mut host);

    // send a FIN (move tcp to the "close-wait" state)
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: "1.2.3.4".parse().unwrap(),
        },
        flags: TcpFlags::FIN,
        src_port: 20,
        dst_port: 10,
        seq: 1,
        ack: 2,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_close_wait().is_some());

    // check the ACK packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::ACK));

    // send on the socket
    TcpSocket::sendmsg(&tcp, &b"hello"[..], 5).unwrap();

    // check the data packet sent by the socket
    let (_, payload) = scheduler.pop_packet().unwrap();
    assert_eq!(payload.concat()[..], b"hello"[..]);

    // close the socket (move tcp to the "last-ack" state)
    tcp.borrow_mut().close().unwrap();
    assert!(s(&tcp).as_last_ack().is_some());

    // check the FIN packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::FIN));

    // send an ACK (move tcp to the "closed" state)
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: "1.2.3.4".parse().unwrap(),
        },
        flags: TcpFlags::ACK,
        src_port: 20,
        dst_port: 10,
        seq: 2,
        ack: 7,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_closed().is_some());
}

#[test]
fn test_active_close_1() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // get an established tcp socket
    let tcp = establish_helper(&scheduler, &mut host);

    // close the socket (move tcp to the "fin-wait-one" state)
    tcp.borrow_mut().close().unwrap();
    assert!(s(&tcp).as_fin_wait_one().is_some());

    // check the FIN packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::FIN));

    // send a ACK (move tcp to the "fin-wait-two" state)
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: "1.2.3.4".parse().unwrap(),
        },
        flags: TcpFlags::ACK,
        src_port: 20,
        dst_port: 10,
        seq: 1,
        ack: 2,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_fin_wait_two().is_some());

    // send a FIN (move tcp to the "time-wait" state)
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: "1.2.3.4".parse().unwrap(),
        },
        flags: TcpFlags::FIN,
        src_port: 20,
        dst_port: 10,
        seq: 1,
        ack: 2,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_time_wait().is_some());

    // check the ACK packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::ACK));

    // at 59 seconds, the socket is still in time-wait
    scheduler.advance(Duration::from_secs(59));
    assert!(s(&tcp).as_time_wait().is_some());

    // at 61 seconds, the event has run and the socket is now closed
    scheduler.advance(Duration::from_secs(2));
    assert!(s(&tcp).as_closed().is_some());
}

#[test]
fn test_active_close_2() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // get an established tcp socket
    let tcp = establish_helper(&scheduler, &mut host);

    // close the socket (move tcp to the "fin-wait-one" state)
    tcp.borrow_mut().close().unwrap();
    assert!(s(&tcp).as_fin_wait_one().is_some());

    // check the FIN packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::FIN));

    // send a FIN-ACK (move tcp to the "time-wait" state)
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: "1.2.3.4".parse().unwrap(),
        },
        flags: TcpFlags::FIN | TcpFlags::ACK,
        src_port: 20,
        dst_port: 10,
        seq: 1,
        ack: 2,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_time_wait().is_some());

    // check the ACK packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::ACK));

    // at 59 seconds, the socket is still in time-wait
    scheduler.advance(Duration::from_secs(59));
    assert!(s(&tcp).as_time_wait().is_some());

    // at 61 seconds, the event has run and the socket is now closed
    scheduler.advance(Duration::from_secs(2));
    assert!(s(&tcp).as_closed().is_some());
}

#[test]
fn test_active_close_3() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // get an established tcp socket
    let tcp = establish_helper(&scheduler, &mut host);

    // close the socket (move tcp to the "fin-wait-one" state)
    tcp.borrow_mut().close().unwrap();
    assert!(s(&tcp).as_fin_wait_one().is_some());

    // check the FIN packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::FIN));

    // send a FIN (move tcp to the "closing" state)
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: "1.2.3.4".parse().unwrap(),
        },
        flags: TcpFlags::FIN,
        src_port: 20,
        dst_port: 10,
        seq: 1,
        ack: 2,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_closing().is_some());

    // check the ACK packet sent by the socket
    let (header, _) = scheduler.pop_packet().unwrap();
    assert!(header.flags.contains(TcpFlags::ACK));

    // send a ACK (move tcp to the "time-wait" state)
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: "1.2.3.4".parse().unwrap(),
        },
        flags: TcpFlags::ACK,
        src_port: 20,
        dst_port: 10,
        seq: 2,
        ack: 2,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_time_wait().is_some());

    // at 59 seconds, the socket is still in time-wait
    scheduler.advance(Duration::from_secs(59));
    assert!(s(&tcp).as_time_wait().is_some());

    // at 61 seconds, the event has run and the socket is now closed
    scheduler.advance(Duration::from_secs(2));
    assert!(s(&tcp).as_closed().is_some());
}
