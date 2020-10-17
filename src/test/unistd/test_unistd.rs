/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::process;

extern {
    pub fn gethostname(name: *mut libc::c_char, size: libc::size_t) -> libc::c_int;
}


fn main() {
    let argv: Vec<String> = std::env::args().collect();
    println!("{:?}", argv);

    if argv.len() < 6 {
        eprintln!("Usage: {} sysname nodename release version machine", argv[0].clone());
        std::process::exit(1);
    }
    let _sysname = argv[1].clone();
    let nodename = argv[2].clone();
    let _release = argv[3].clone();
    let _version = argv[4].clone();
    let _machine = argv[5].clone();

    test_getpid_nodeps();
    test_gethostname(nodename);
}


// Tests that the results are plausible, but can't really validate that it's our
// pid without depending on other functionality.
fn test_getpid_nodeps() {
    let pid = process::id();
    assert!(pid > 0);
    assert_eq!(pid, process::id());
}


fn test_gethostname(nodename: String) {
    let hostname = get_gethostname();

    assert_eq!(hostname, nodename);
}


fn get_gethostname() -> String {
    let mut buffer = vec![0 as u8; 1000];
    let err = unsafe {
        gethostname (buffer.as_mut_ptr() as *mut libc::c_char, buffer.len())
    };
    assert_eq!(err, 0);

    let hostname_len = buffer.iter().position(|&byte| byte == 0).unwrap_or(buffer.len());
    buffer.resize(hostname_len, 0);

    match String::from_utf8(buffer) {
        Ok(hostname) => hostname,
        _ => panic!("Error on String convertion")
    }
}
