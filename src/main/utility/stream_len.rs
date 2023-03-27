use std::io::{Seek, SeekFrom};

pub trait StreamLen {
    /// Backport of std::io::Seek::stream_len from Rust nightly.
    fn stream_len_bp(&mut self) -> std::io::Result<u64>;
}

impl<T> StreamLen for T
where
    T: Seek,
{
    fn stream_len_bp(&mut self) -> std::io::Result<u64> {
        let current = self.stream_position()?;
        let end = self.seek(SeekFrom::End(0))?;
        if current != end {
            self.seek(SeekFrom::Start(current))?;
        }
        Ok(end)
    }
}

#[cfg(test)]
mod tests {
    use std::io::Cursor;

    use super::*;

    #[test]
    fn test_stream_len_bp() -> std::io::Result<()> {
        let data = [0, 1, 2, 3, 4, 5];
        let mut cursor = Cursor::new(&data);

        assert_eq!(cursor.stream_len_bp()?, 6);
        assert_eq!(cursor.position(), 0);

        cursor.seek(SeekFrom::Start(2))?;
        assert_eq!(cursor.stream_len_bp()?, 6);
        assert_eq!(cursor.position(), 2);

        cursor.seek(SeekFrom::End(0))?;
        assert_eq!(cursor.stream_len_bp()?, 6);
        assert_eq!(cursor.position(), 6);
        Ok(())
    }
}
