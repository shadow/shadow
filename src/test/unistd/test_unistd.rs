/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::process;


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
    let hostname = gethostname::gethostname().into_string().unwrap();
    assert_eq!(hostname, nodename);
}
