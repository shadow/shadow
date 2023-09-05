//! Test different window scale configurations.

use std::cell::{Ref, RefCell};
use std::rc::Rc;

use crate::tests::{Host, Scheduler, TcpSocket, TestEnvState};
use crate::{Ipv4Header, Payload, TcpConfig, TcpFlags, TcpHeader, TcpState};

#[test]
fn test_peer_no_window_scaling() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling enabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = true;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN and check that it sent a non-zero window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);
    assert!(response_header.window_scale.unwrap() > 0);

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send the SYN+ACK without a window scale option
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

    let tcp_ref = s(&tcp);
    let connection = &tcp_ref.as_established().unwrap().connection;

    // make sure that it's not applying any window scaling
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.recv_window_scale_shift(), 0);
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 0);
}

#[test]
fn test_local_no_window_scaling() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling disabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = false;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN and check that it sent a non-zero window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);
    assert!(response_header.window_scale.is_none());

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send the SYN+ACK without a window scale option
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
        window_scale: Some(3),
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);

    let tcp_ref = s(&tcp);
    let connection = &tcp_ref.as_established().unwrap().connection;

    // make sure that it's not applying any window scaling
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.recv_window_scale_shift(), 0);
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 0);
}

#[test]
fn test_both_without_window_scaling() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling disabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = false;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN and check that it sent a non-zero window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);
    assert!(response_header.window_scale.is_none());

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send the SYN+ACK without a window scale option
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

    let tcp_ref = s(&tcp);
    let connection = &tcp_ref.as_established().unwrap().connection;

    // make sure that it's not applying any window scaling
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.recv_window_scale_shift(), 0);
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 0);
}

#[test]
fn test_both_with_window_scaling() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling enabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = true;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN and check that it sent a non-zero window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    let response_window_scale = response_header.window_scale.unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send the SYN+ACK without a window scale option
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
        window_scale: Some(3),
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);

    let tcp_ref = s(&tcp);
    let connection = &tcp_ref.as_established().unwrap().connection;

    // make sure that the window scaling is as expected
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 3);
    assert_eq!(
        connection.window_scaling.recv_window_scale_shift(),
        response_window_scale
    );
}

#[test]
fn test_large_window_scale() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling enabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = true;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN and check that it sent a non-zero window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);
    assert!(response_header.window_scale.is_some());

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send the SYN+ACK with a window scale option that's too large
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
        window_scale: Some(15),
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);

    let tcp_ref = s(&tcp);
    let connection = &tcp_ref.as_established().unwrap().connection;

    // make sure that the window scaling was lowered to 14
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 14);
}

/// Test that the socket does not send a window scale option if the SYN packet it received did not
/// have a window scale option set.
#[test]
fn test_window_scale_after_receiving_syn_without() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling enabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = true;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::listen(&tcp, &mut host, 10).unwrap();
    assert!(s(&tcp).as_listen().is_some());

    // send the SYN without a window scale option
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

    // read the SYN+ACK and make sure it didn't set the window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN | TcpFlags::ACK);
    assert!(response_header.window_scale.is_none());

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

    // the connection is now established so can accept it
    let accepted_socket = tcp.borrow_mut().accept(&mut host).unwrap();
    assert!(s(&accepted_socket).as_established().is_some());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 0);

    let accepted_socket_ref = s(&accepted_socket);
    let connection = &accepted_socket_ref.as_established().unwrap().connection;

    // make sure that it's not applying any window scaling
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.recv_window_scale_shift(), 0);
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 0);
}

/// Test that the socket does send a window scale option if the SYN packet it received did have a
/// window scale option set.
#[test]
fn test_window_scale_after_receiving_syn_with() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling enabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = true;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::listen(&tcp, &mut host, 10).unwrap();
    assert!(s(&tcp).as_listen().is_some());

    // send the SYN with a window scale option
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
        window_scale: Some(3),
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // read the SYN+ACK and make sure it did set the window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    let response_window_scale = response_header.window_scale.unwrap();
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

    // the connection is now established so can accept it
    let accepted_socket = tcp.borrow_mut().accept(&mut host).unwrap();
    assert!(s(&accepted_socket).as_established().is_some());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 0);

    let accepted_socket_ref = s(&accepted_socket);
    let connection = &accepted_socket_ref.as_established().unwrap().connection;

    // make sure that the window scaling is as expected
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 3);
    assert_eq!(
        connection.window_scaling.recv_window_scale_shift(),
        response_window_scale
    );
}

#[test]
fn test_duplicate_syn_with_different_window_scale() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    // make sure it has window scaling enabled
    let mut config = TcpConfig::default();
    config.window_scaling_enabled = true;

    let tcp = TcpSocket::new(&scheduler, config);
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::connect(&tcp, "5.6.7.8:10".parse().unwrap(), &mut host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN and check that it sent a non-zero window scale option
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);
    assert!(response_header.window_scale.is_some());

    // get the autobind address of the socket
    let tcp_bind_addr = response_header.src();

    // send the SYN+ACK with a window scale option
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
        window_scale: Some(3),
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());

    // send the SYN+ACK again with a different window scale
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
        window_scale: Some(5),
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);

    let tcp_ref = s(&tcp);
    let connection = &tcp_ref.as_established().unwrap().connection;

    // make sure that the window scaling was not changed by the duplicate SYN
    assert!(connection.window_scaling.is_configured());
    assert_eq!(connection.window_scaling.send_window_scale_shift(), 3);
}
