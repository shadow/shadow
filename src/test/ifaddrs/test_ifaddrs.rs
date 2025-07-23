/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

fn main() {
    // Call getifaddrs to get a list of available IP addresses.
    let addrs = nix::ifaddrs::getifaddrs().unwrap();

    // TODO: in case someday we want to try to use libc instead of nix here
    //let mut ifap: *mut libc::ifaddrs = std::ptr::null_mut();
    //let ifap_ptr: *mut *mut libc::ifaddrs = &mut ifap;
    //let err = unsafe { libc::getifaddrs(ifap_ptr) };
    //assert_eq!(err, 0);

    let mut ip_vec = vec![];
    for ifaddr in addrs {
        let address: nix::sys::socket::SockaddrStorage = match ifaddr.address {
            Some(a) => a,
            None => {
                println!("Skipping non-sockaddr {ifaddr:?}");
                continue;
            }
        };
        let address: &nix::sys::socket::SockaddrIn = match address.as_sockaddr_in() {
            Some(a) => a,
            None => {
                println!("Skipping non-ipv4 address {address}");
                continue;
            }
        };

        // if there was an ipv4 address, the netmask should also be an ipv4 address
        let netmask: nix::sys::socket::SockaddrStorage = ifaddr.netmask.unwrap();
        let netmask: &nix::sys::socket::SockaddrIn = netmask.as_sockaddr_in().unwrap();

        let address_str = address.to_string();
        let ip = address_str.split(':').collect::<Vec<&str>>()[0];

        let netmask = netmask.to_string();
        let netmask = netmask.split(':').collect::<Vec<&str>>()[0];

        println!(
            "found ifaddr interface {} address {} netmask {:?}",
            ifaddr.interface_name, ip, netmask
        );
        ip_vec.push(String::from(ip));
    }

    for argument in std::env::args().skip(1) {
        println!("checking that ip address argument {argument} is in ifaddrs list");
        assert!(ip_vec.contains(&argument));
    }
}
