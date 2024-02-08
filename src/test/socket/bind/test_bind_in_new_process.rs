fn main() {
    // use libc rather than nix so that we don't accidentally close the socket due to a
    // Drop impl or something

    // first and only arg is the number of seconds to sleep for
    let sleep_time: u32 = std::env::args().nth(1).unwrap().parse().unwrap();

    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_ANY.to_be(),
        },
        sin_zero: [0; 8],
    };

    let rv = unsafe {
        libc::bind(
            fd,
            std::ptr::from_ref(&addr) as *const libc::sockaddr,
            std::mem::size_of_val(&addr) as u32,
        )
    };

    assert_eq!(rv, 0);

    unsafe { libc::sleep(sleep_time) };
}
