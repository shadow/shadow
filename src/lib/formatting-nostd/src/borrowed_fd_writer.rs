use rustix::fd::BorrowedFd;

/// A `core::fmt::Writer` that writes to a file descriptor via direct syscalls.
///
/// Its `core::fmt::Write` implementation retries if interrupted by a signal,
/// and returns errors if the file is closed or the write returns other errors
/// (including `EWOULDBLOCK`). In such cases, partial writes can occur.
///
/// To format a message with Rust's formatting:
/// ```
/// # // Can't create pipes under miri.
/// # #[cfg(not(miri))]
/// # {
/// # use formatting_nostd::BorrowedFdWriter;
/// use rustix::fd::AsFd;
/// use core::fmt::Write;
/// let (_reader_fd, writer_fd) = rustix::pipe::pipe().unwrap();
/// let mut writer = BorrowedFdWriter::new(writer_fd.as_fd());
/// let x = 42;
/// write!(&mut writer, "{x}").unwrap();
/// # }
/// ```
pub struct BorrowedFdWriter<'fd> {
    fd: BorrowedFd<'fd>,
}

impl<'fd> BorrowedFdWriter<'fd> {
    pub fn new(fd: BorrowedFd<'fd>) -> Self {
        Self { fd }
    }
}

impl core::fmt::Write for BorrowedFdWriter<'_> {
    fn write_str(&mut self, s: &str) -> Result<(), core::fmt::Error> {
        let mut bytes_slice = s.as_bytes();
        while !bytes_slice.is_empty() {
            let Ok(written) = rustix::io::retry_on_intr(|| rustix::io::write(self.fd, bytes_slice))
            else {
                return Err(core::fmt::Error);
            };
            if written == 0 {
                // Not making forward progress; e.g. file may be closed.
                return Err(core::fmt::Error);
            }
            bytes_slice = &bytes_slice[written..];
        }
        Ok(())
    }
}

// We can't test without going through FFI, which miri doesn't support.
#[cfg(all(test, not(miri)))]
mod test {
    use core::fmt::Write;

    use rustix::fd::AsFd;

    use super::*;

    #[test]
    fn test_write() {
        let (reader, writer) = rustix::pipe::pipe().unwrap();

        BorrowedFdWriter::new(writer.as_fd())
            .write_str("123")
            .unwrap();

        let mut buf = [0xff; 4];
        assert_eq!(rustix::io::read(reader.as_fd(), &mut buf), Ok(3));
        assert_eq!(buf, [b'1', b'2', b'3', 0xff]);
    }
}
