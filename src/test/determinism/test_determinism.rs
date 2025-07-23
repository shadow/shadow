use std::{ffi::CStr, io::Read};

use rand::RngCore as _;

fn test_open_read(filename: &str) {
    let mut f = std::fs::File::open(filename).unwrap();
    let mut buf = [0u8; 4];
    f.read_exact(&mut buf).unwrap();
    println!("{filename} bytes: {buf:?}");
}

fn test_getauxval_at_random() {
    let at_random = unsafe { libc::getauxval(libc::AT_RANDOM) } as *mut u8;
    assert!(!at_random.is_null());
    let buf = unsafe { std::slice::from_raw_parts(at_random, 16) };
    println!("getauxval(AT_RANDOM) bytes: {buf:?}");
}

fn test_threads() {
    let handles = (0..10)
        .map(|i| {
            std::thread::spawn(move || {
                let pid = unsafe { libc::getpid() };
                let ppid = unsafe { libc::getppid() };
                let tid = unsafe { libc::syscall(libc::SYS_gettid) };
                println!("thread:{i}: pid:{pid} ppid:{ppid} tid:{tid}");
            })
        })
        .collect::<Vec<_>>();

    for handle in handles {
        handle.join().unwrap();
    }
}

fn test_name_address() {
    let mut hostname = [0u8; 1024];
    let res = unsafe { libc::gethostname(hostname.as_mut_ptr().cast(), hostname.len()) };
    assert_eq!(res, 0);
    println!(
        "gethostname: {}",
        CStr::from_bytes_until_nul(&hostname)
            .unwrap()
            .to_str()
            .unwrap()
    );

    {
        let mut addrinfo_p: *mut libc::addrinfo = std::ptr::null_mut();
        let res = unsafe {
            libc::getaddrinfo(
                hostname.as_ptr().cast(),
                std::ptr::null(),
                std::ptr::null(),
                &mut addrinfo_p,
            )
        };
        assert_eq!(res, 0);
        let addrinfo = unsafe { addrinfo_p.as_ref() }.unwrap();
        match addrinfo.ai_family {
            libc::AF_INET => {
                let sockaddr_in =
                    unsafe { addrinfo.ai_addr.cast::<libc::sockaddr_in>().as_ref() }.unwrap();
                let addr = std::net::Ipv4Addr::from_bits(u32::from_be(sockaddr_in.sin_addr.s_addr));
                println!("getaddrinfo(hostname): {addr:?}");
            }
            libc::AF_INET6 => {
                let sockaddr_in =
                    unsafe { addrinfo.ai_addr.cast::<libc::sockaddr_in6>().as_ref() }.unwrap();
                let addr = std::net::Ipv6Addr::from_bits(u128::from_ne_bytes(
                    sockaddr_in.sin6_addr.s6_addr,
                ));
                println!("getaddrinfo(hostname): {addr:?}");
            }
            x => {
                panic!("Unrecognized family {x}")
            }
        }
        unsafe { libc::freeaddrinfo(addrinfo_p) };
    }
}

fn test_uuid() {
    let path = "/proc/sys/kernel/random/uuid";
    let mut f = std::fs::File::open(path).unwrap();
    let mut contents = Vec::new();
    f.read_to_end(&mut contents).unwrap();
    println!(
        "contents of {path}: {}",
        String::from_utf8(contents).unwrap()
    );
}

fn test_tor_llcrypto() {
    if unsafe { asm_util::cpuid::supports_rdrand() }
        || unsafe { asm_util::cpuid::supports_rdseed() }
    {
        // Really we want to know if the host platform supports trapping cpuid,
        // and only skip this test if it doesn't. We can't check that from here
        // within shadow, though, since shadow always reports that it doesn't
        // support it to arch_prctl.
        // TODO: cmake plumbing to compile and run a test program to determine
        // this, and then pass through e.g. a command-line parameter to this
        // test.
        println!(
            "cpuid reports rdrand or rdseed support, possibly because platform doesn't support trapping cpuid. Skipping tor_llcrypto test"
        );
        return;
    }
    let mut rng = tor_llcrypto::rng::CautiousRng;
    println!("cautiousrng: {:x}", rng.next_u64())
}

pub fn main() {
    test_open_read("/dev/random");
    test_open_read("/dev/urandom");
    test_getauxval_at_random();
    test_threads();
    test_name_address();
    test_uuid();
    test_tor_llcrypto();
}
