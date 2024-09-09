use std::io::Write;

// this code has been adapted from the `Take` implementation

/// Writer adapter which limits the bytes written to an underlying writer.
///
/// Intended to be a [`std::io::Write`] counterpart to [`std::io::Take`] for [`std::io::Read`]. This
/// does not force the write limit, for example `give.get_mut()` can be used to write directly to
/// the underlying writer without updating the internal state of the `Give` object.
pub struct Give<T> {
    inner: T,
    limit: u64,
}

impl<T> Give<T> {
    pub fn new(writer: T, limit: u64) -> Self {
        Self {
            inner: writer,
            limit,
        }
    }

    /// Returns the number of bytes that can be written before this instance will return a write of
    /// 0 bytes.
    pub fn limit(&self) -> u64 {
        self.limit
    }

    /// Sets the number of bytes that can be read before this instance will return a write of 0
    /// bytes. This is the same as constructing a new `Give` instance, so the amount of bytes read
    /// and the previous limit value don't matter when calling this method.
    pub fn set_limit(&mut self, limit: u64) {
        self.limit = limit;
    }

    /// Consumes the `Give`, returning the wrapped writer.
    pub fn into_inner(self) -> T {
        self.inner
    }

    /// Gets a reference to the underlying writer.
    pub fn get_ref(&self) -> &T {
        &self.inner
    }

    /// Gets a mutable reference to the underlying writer.
    pub fn get_mut(&mut self) -> &mut T {
        &mut self.inner
    }
}

impl<T: Write> Write for Give<T> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        // don't call into the inner writer at all at EOF because it may still block
        if self.limit == 0 {
            return Ok(0);
        }

        let max = std::cmp::min(buf.len() as u64, self.limit) as usize;
        let n = self.inner.write(&buf[..max])?;
        self.limit -= n as u64;
        Ok(n)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.inner.flush()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_give_write_1() {
        let mut buf = vec![];
        let mut give = Give::new(&mut buf, 5);
        assert_eq!(give.write(&[0u8; 20]).unwrap(), 5);
        assert_eq!(give.write(&[0u8; 20]).unwrap(), 0);
        assert_eq!(buf.len(), 5);
    }

    #[test]
    fn test_give_write_2() {
        let mut buf = vec![];
        let mut give = Give::new(&mut buf, 5);
        assert_eq!(give.write(&[0u8; 3]).unwrap(), 3);
        assert_eq!(give.write(&[0u8; 3]).unwrap(), 2);
        assert_eq!(give.write(&[0u8; 3]).unwrap(), 0);
        assert_eq!(buf.len(), 5);
    }

    #[test]
    fn test_give_write_all_1() {
        let mut buf = vec![];
        let mut give = Give::new(&mut buf, 5);
        assert!(give.write_all(&[0u8; 3]).is_ok());
        assert!(give.write_all(&[0u8; 2]).is_ok());
        assert!(give.write_all(&[0u8; 1]).is_err());
        assert_eq!(buf.len(), 5);
    }

    #[test]
    fn test_give_write_all_2() {
        let mut buf = vec![];
        let mut give = Give::new(&mut buf, 5);
        assert!(give.write_all(&[0u8; 7]).is_err());
        assert!(give.write_all(&[0u8; 1]).is_err());
        assert!(give.write_all(&[0u8; 7]).is_err());
        assert!(give.write_all(&[0u8; 1]).is_err());
        assert!(give.write_all(&[0u8; 3]).is_err());
        assert_eq!(buf.len(), 5);
    }
}
