use crate::get_errno;

/// A container for different types of socket addresses.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum SockAddr {
    Generic(libc::sockaddr_storage),
    Inet(libc::sockaddr_in),
    Unix(libc::sockaddr_un),
}

impl SockAddr {
    /// Initialize the inet socket address struct with invalid/dummy data.
    pub fn dummy_init_inet() -> Self {
        SockAddr::Inet(libc::sockaddr_in {
            sin_family: 123u16,
            sin_port: 456u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: 789u32.to_be(),
            },
            sin_zero: [1; 8],
        })
    }

    /// Initialize the unix socket address struct with invalid/dummy data.
    pub fn dummy_init_unix() -> Self {
        SockAddr::Unix(libc::sockaddr_un {
            sun_family: 123u16,
            sun_path: [1; 108],
        })
    }

    /// Initialize the generic socket address struct with invalid/dummy data.
    pub fn dummy_init_generic() -> Self {
        let addr_len = std::mem::size_of::<libc::sockaddr_storage>();
        let bytes: Vec<u8> = (0..(addr_len.try_into().unwrap())).collect();
        assert_eq!(bytes.len(), addr_len);
        SockAddr::Generic(unsafe { std::ptr::read_unaligned(bytes.as_ptr() as *const _) })
    }

    /// Get the pointer to the sockaddr struct.
    pub fn as_ptr(&self) -> *const libc::sockaddr {
        match self {
            Self::Generic(ref x) => std::ptr::from_ref(x) as *const _,
            Self::Inet(ref x) => std::ptr::from_ref(x) as *const _,
            Self::Unix(ref x) => std::ptr::from_ref(x) as *const _,
        }
    }

    /// Get the mutable pointer to the sockaddr struct.
    pub fn as_mut_ptr(&mut self) -> *mut libc::sockaddr {
        match self {
            Self::Generic(ref mut x) => std::ptr::from_mut(x) as *mut _,
            Self::Inet(ref mut x) => std::ptr::from_mut(x) as *mut _,
            Self::Unix(ref mut x) => std::ptr::from_mut(x) as *mut _,
        }
    }

    /// The size (number of bytes) of the sockaddr.
    pub fn ptr_size(&self) -> libc::socklen_t {
        match self {
            Self::Generic(ref x) => std::mem::size_of_val(x) as u32,
            Self::Inet(ref x) => std::mem::size_of_val(x) as u32,
            Self::Unix(ref x) => std::mem::size_of_val(x) as u32,
        }
    }

    pub fn as_slice(&self) -> &[u8] {
        let ptr = self.as_ptr();
        let len = self.ptr_size();
        unsafe { std::slice::from_raw_parts(ptr as *const u8, len as usize) }
    }

    pub fn as_generic(&self) -> Option<&libc::sockaddr_storage> {
        match self {
            Self::Generic(ref x) => Some(x),
            _ => None,
        }
    }

    pub fn as_generic_mut(&mut self) -> Option<&mut libc::sockaddr_storage> {
        match self {
            Self::Generic(ref mut x) => Some(x),
            _ => None,
        }
    }

    pub fn as_inet(&self) -> Option<&libc::sockaddr_in> {
        match self {
            Self::Inet(ref x) => Some(x),
            _ => None,
        }
    }

    pub fn as_inet_mut(&mut self) -> Option<&mut libc::sockaddr_in> {
        match self {
            Self::Inet(ref mut x) => Some(x),
            _ => None,
        }
    }

    pub fn as_unix(&self) -> Option<&libc::sockaddr_un> {
        match self {
            Self::Unix(ref x) => Some(x),
            _ => None,
        }
    }

    pub fn as_unix_mut(&mut self) -> Option<&mut libc::sockaddr_un> {
        match self {
            Self::Unix(ref mut x) => Some(x),
            _ => None,
        }
    }
}

impl From<libc::sockaddr_in> for SockAddr {
    fn from(addr: libc::sockaddr_in) -> Self {
        Self::Inet(addr)
    }
}

impl From<libc::sockaddr_un> for SockAddr {
    fn from(addr: libc::sockaddr_un) -> Self {
        Self::Unix(addr)
    }
}

impl From<libc::sockaddr_storage> for SockAddr {
    fn from(addr: libc::sockaddr_storage) -> Self {
        Self::Generic(addr)
    }
}

#[derive(PartialEq, Eq, Copy, Clone, Debug)]
pub enum SocketInitMethod {
    Inet,
    Unix,
    UnixSocketpair,
}

impl SocketInitMethod {
    pub fn domain(&self) -> libc::c_int {
        match self {
            Self::Inet => libc::AF_INET,
            Self::Unix => libc::AF_UNIX,
            Self::UnixSocketpair => libc::AF_UNIX,
        }
    }
}

/// A helper function to initialize two sockets such that the first socket is connected to the
/// second socket. The second socket may or may not be connected to the first socket.
pub fn socket_init_helper(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flags: libc::c_int,
    bind_client: bool,
) -> (libc::c_int, libc::c_int) {
    match init_method {
        SocketInitMethod::UnixSocketpair => {
            // get two connected unix sockets
            let mut fds = vec![-1 as libc::c_int; 2];
            assert_eq!(0, unsafe {
                libc::socketpair(init_method.domain(), sock_type | flags, 0, fds.as_mut_ptr())
            });

            let (fd_client, fd_peer) = (fds[0], fds[1]);
            assert!(fd_client >= 0 && fd_peer >= 0);

            (fd_client, fd_peer)
        }
        SocketInitMethod::Inet | SocketInitMethod::Unix => {
            let fd_client = unsafe { libc::socket(init_method.domain(), sock_type | flags, 0) };
            let fd_server = unsafe { libc::socket(init_method.domain(), sock_type | flags, 0) };
            assert!(fd_client >= 0);
            assert!(fd_server >= 0);

            // bind the server socket to some unused address
            let (server_addr, server_addr_len) = autobind_helper(fd_server, init_method.domain());

            if bind_client {
                // bind the client socket to some unused address
                autobind_helper(fd_client, init_method.domain());
            }

            match sock_type {
                libc::SOCK_STREAM | libc::SOCK_SEQPACKET => {
                    // connect the client to the server and get the accepted socket
                    let fd_peer = stream_connect_helper(
                        fd_client,
                        fd_server,
                        server_addr,
                        server_addr_len,
                        flags,
                    );

                    // close the server
                    assert_eq!(0, unsafe { libc::close(fd_server) });

                    (fd_client, fd_peer)
                }
                libc::SOCK_DGRAM => {
                    // connect the client to the server
                    dgram_connect_helper(fd_client, server_addr, server_addr_len);
                    (fd_client, fd_server)
                }
                _ => unimplemented!(),
            }
        }
    }
}

/// A helper function to connect the client socket to the server address. Returns the accepted
/// socket.
pub fn stream_connect_helper(
    fd_client: libc::c_int,
    fd_server: libc::c_int,
    addr: SockAddr,
    addr_len: libc::socklen_t,
    flags: libc::c_int,
) -> libc::c_int {
    // listen for connections
    {
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    let addr_ptr = match addr {
        SockAddr::Inet(ref x) => std::ptr::from_ref(x) as *const libc::sockaddr,
        SockAddr::Unix(ref x) => std::ptr::from_ref(x) as *const libc::sockaddr,
        _ => unimplemented!(),
    };

    // connect to the server address
    {
        let rv = unsafe { libc::connect(fd_client, addr_ptr, addr_len) };
        assert!(rv == 0 || (rv == -1 && get_errno() == libc::EINPROGRESS));
    }

    // if non-blocking, shadow needs to run events, otherwise the accept call won't know it has an
    // incoming connection (SYN packet)
    if (flags & libc::SOCK_NONBLOCK) != 0 {
        let rv = unsafe { libc::usleep(10000) };
        assert_eq!(rv, 0);
    }

    // accept the connection
    let fd = unsafe { libc::accept4(fd_server, std::ptr::null_mut(), std::ptr::null_mut(), flags) };
    assert!(fd >= 0);

    fd
}

/// A helper function to connect the client socket to the server address.
pub fn dgram_connect_helper(fd_client: libc::c_int, addr: SockAddr, addr_len: libc::socklen_t) {
    let addr_ptr = match addr {
        SockAddr::Inet(ref x) => std::ptr::from_ref(x) as *const libc::sockaddr,
        SockAddr::Unix(ref x) => std::ptr::from_ref(x) as *const libc::sockaddr,
        _ => unimplemented!(),
    };

    // connect to the server address
    let rv = unsafe { libc::connect(fd_client, addr_ptr, addr_len) };
    assert_eq!(rv, 0);
}

/// A helper function to autobind the socket to some unused address. Returns the address.
pub fn autobind_helper(fd: libc::c_int, domain: libc::c_int) -> (SockAddr, libc::socklen_t) {
    // get the autobind address for the given domain
    let (mut server_addr, server_addr_len) = match domain {
        libc::AF_INET => (
            // an inet sockaddr with a port of 0
            SockAddr::Inet(libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 0u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            }),
            std::mem::size_of::<libc::sockaddr_in>() as libc::socklen_t,
        ),
        libc::AF_UNIX => (
            // a unix sockaddr with length 2 (an unnamed unix sockaddr)
            SockAddr::Unix(libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0; 108],
            }),
            2,
        ),
        _ => unimplemented!(),
    };

    let server_addr_ptr = match server_addr {
        SockAddr::Inet(ref mut x) => std::ptr::from_mut(x) as *mut libc::sockaddr,
        SockAddr::Unix(ref mut x) => std::ptr::from_mut(x) as *mut libc::sockaddr,
        _ => unimplemented!(),
    };

    // bind on the autobind address
    {
        let rv = unsafe { libc::bind(fd, server_addr_ptr, server_addr_len) };
        assert_eq!(rv, 0);
    }

    // reset the address length to the max possible length
    let mut server_addr_len = match server_addr {
        SockAddr::Inet(ref x) => std::mem::size_of_val(x) as libc::socklen_t,
        SockAddr::Unix(ref x) => std::mem::size_of_val(x) as libc::socklen_t,
        _ => unimplemented!(),
    };

    // get the assigned address
    {
        let max_len = server_addr_len;
        let rv = unsafe {
            libc::getsockname(
                fd,
                server_addr_ptr,
                std::ptr::from_mut(&mut server_addr_len),
            )
        };
        assert_eq!(rv, 0);
        assert!(server_addr_len <= max_len);
    }

    (server_addr, server_addr_len)
}

/// A helper function to get the peername of `fd_peer` and connect `fd_client` to it.
///
/// This does not call `accept()`, so you must do that manually if using a connection-oriented
/// socket. Returns the return value of the `connect()`.
pub fn connect_to_peername(fd_client: libc::c_int, fd_peer: libc::c_int) -> libc::c_int {
    let mut addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };
    let mut addr_len: libc::socklen_t = std::mem::size_of_val(&addr).try_into().unwrap();

    // get the peer address
    {
        let rv = unsafe {
            libc::getsockname(
                fd_peer,
                std::ptr::from_mut(&mut addr) as *mut libc::sockaddr,
                &mut addr_len,
            )
        };
        assert_eq!(rv, 0);
    }

    // connect to the peer address
    unsafe {
        libc::connect(
            fd_client,
            std::ptr::from_ref(&addr) as *const libc::sockaddr,
            addr_len,
        )
    }
}
