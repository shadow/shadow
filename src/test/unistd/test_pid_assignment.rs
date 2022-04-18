pub fn main() {
    let argv: Vec<String> = std::env::args().collect();
    let expected_pid = argv[1].parse::<libc::pid_t>().unwrap();
    assert_eq!(unsafe { libc::getpid() }, expected_pid);
}
