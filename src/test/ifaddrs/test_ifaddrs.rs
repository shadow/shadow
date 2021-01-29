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
        match ifaddr.address {
            Some(address) => {
                let address_str = address.to_str();
                let ip = address_str.split(":").collect::<Vec<&str>>()[0];
                println!(
                    "found ifaddr interface {} address {}",
                    ifaddr.interface_name, ip
                );
                ip_vec.push(String::from(ip));
            }
            None => {}
        }
    }

    for argument in std::env::args().skip(1) {
        println!(
            "checking that ip address argument {} is in ifaddrs list",
            argument
        );
        assert!(ip_vec.contains(&argument));
    }
}
