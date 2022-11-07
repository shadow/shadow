use std::io::Write;
use std::net::ToSocketAddrs;

use nix::errno::Errno;
use nix::sys::socket::{sockopt::RcvBuf, AddressFamily, MsgFlags, SockFlag, SockType};

const NUM_MSGS: usize = 100;
const MSG_SIZE: usize = 100;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arg = std::env::args().nth(1).ok_or("Missing arg[1]")?;
    let addr_or_port = std::env::args().nth(2).ok_or("Missing arg[2]")?;

    match arg.as_str() {
        "sender" => run_sender(addr_or_port.to_socket_addrs()?.next().unwrap())?,
        "receiver" => run_receiver(addr_or_port.parse()?)?,
        _ => return Err("Invalid argument".into()),
    };

    Ok(())
}

fn run_sender(addr: std::net::SocketAddr) -> Result<(), Box<dyn std::error::Error>> {
    let fd = nix::sys::socket::socket(
        AddressFamily::Inet,
        SockType::Datagram,
        SockFlag::SOCK_NONBLOCK,
        None,
    )?;

    nix::sys::socket::connect(fd, &nix::sys::socket::SockaddrStorage::from(addr))?;

    // send NUM_MSGS udp messages
    for idx in 0..NUM_MSGS {
        let mut bytes = [0u8; MSG_SIZE];

        // start the buffer with the text value of idx (helpful when debugging)
        write!(bytes.as_mut_slice(), "{idx}").unwrap();

        let num_sent = nix::sys::socket::send(fd, &bytes, MsgFlags::empty())?;
        assert_eq!(num_sent, NUM_MSGS);
    }

    // we should probably have set SO_LINGER for the socket, but Shadow doesn't support that yet
    nix::unistd::close(fd)?;

    Ok(())
}

fn run_receiver(port: u16) -> Result<(), Box<dyn std::error::Error>> {
    let fd = nix::sys::socket::socket(
        AddressFamily::Inet,
        SockType::Datagram,
        SockFlag::SOCK_NONBLOCK,
        None,
    )?;

    // shadow has a minimum buf size of 2048
    nix::sys::socket::setsockopt(fd, RcvBuf, &0)?;
    let recv_buffer_size = nix::sys::socket::getsockopt(fd, RcvBuf)?;

    println!("Receive buffer size: {recv_buffer_size}");

    // make sure the recv buffer size is smaller than the total amount of data we receive
    assert!(recv_buffer_size < NUM_MSGS * MSG_SIZE);

    let bind_addr = nix::sys::socket::SockaddrIn::new(0, 0, 0, 0, port);
    nix::sys::socket::bind(fd, &bind_addr)?;

    // wait for the first packet
    let mut poll_fds = [nix::poll::PollFd::new(fd, nix::poll::PollFlags::POLLIN)];
    assert_eq!(1, nix::poll::poll(&mut poll_fds, -1)?);

    let mut num_bytes_received = 0;
    let mut num_bytes_received_since_eagain = 0;

    // The socket's receive buffer should be full and we should be able to call recv() repeatedly to
    // read all of those messages before receiving EAGAIN. We then need to block using poll() so
    // that Shadow will re-fill the socket's receive buffer, and then we should be able to read
    // those messages and repeat the cycle. No messages should be dropped.

    loop {
        match nix::sys::socket::recv(fd, &mut [0; MSG_SIZE], MsgFlags::empty()) {
            Ok(0) => panic!("Unexpected EOF"),
            Ok(x) => {
                num_bytes_received += x;
                num_bytes_received_since_eagain += x;

                if num_bytes_received == MSG_SIZE * NUM_MSGS {
                    // we've received all the data
                    break;
                }
            }
            Err(Errno::EAGAIN) => {
                // should have received at most 'recv_buffer_size' bytes, but rounded down to
                // the nearest multiple of 'MSG_SIZE'
                assert_eq!(
                    num_bytes_received_since_eagain,
                    MSG_SIZE * (recv_buffer_size / MSG_SIZE)
                );

                num_bytes_received_since_eagain = 0;

                // if packets were dropped, this would eventually block forever
                let mut poll_fds = [nix::poll::PollFd::new(fd, nix::poll::PollFlags::POLLIN)];
                assert_eq!(1, nix::poll::poll(&mut poll_fds, -1)?);
            }
            Err(e) => return Err(e.into()),
        }
    }

    nix::unistd::close(fd)?;

    // we received all of the data
    println!("Bytes received: {num_bytes_received}");
    assert_eq!(num_bytes_received, MSG_SIZE * NUM_MSGS);

    // we should have seen at least one EAGAIN (since 'num_bytes_received' > 'recv_buffer_size'), so
    // these should not be equal
    assert_ne!(num_bytes_received, num_bytes_received_since_eagain);

    Ok(())
}
