/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let stdin_fd = libc::STDIN_FILENO;
    let stdout_fd = libc::STDOUT_FILENO;
    let stderr_fd = libc::STDERR_FILENO;

    // should be able to read from stdin, and since in shadow it's /dev/null, should read 0 bytes
    assert_eq!(0, nix::unistd::read(stdin_fd, &mut [0u8; 1])?);

    // should not be able to write to stdin since we open it with O_RDONLY
    assert!(nix::unistd::write(stdin_fd, &[0u8; 1]).is_err());

    // should be able to write to stdout and stderr
    assert_eq!(1, nix::unistd::write(stdout_fd, &[0u8; 1])?);
    assert_eq!(1, nix::unistd::write(stderr_fd, &[0u8; 1])?);

    // should not be able to read from stdout and stderr since we open them with O_WRONLY
    assert!(nix::unistd::read(stdout_fd, &mut [0u8; 1]).is_err());
    assert!(nix::unistd::read(stderr_fd, &mut [0u8; 1]).is_err());

    Ok(())
}
