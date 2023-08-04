// A path is up to 256 bytes; an isize is 20 bytes; and one byte for delimiter and null terminator.
pub const STRING_BUF_NBYTES: usize = 256 + 20 + 1 + 1;

#[repr(transparent)]
pub struct StringBuf(pub [u8; STRING_BUF_NBYTES]);

impl core::fmt::Display for StringBuf {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let s = if let Some(p) = self.0.iter().position(|x| *x == 0) {
            core::str::from_utf8(&self.0[0..p]).unwrap()
        } else {
            core::str::from_utf8(&self.0[0..]).unwrap()
        };

        write!(f, "{}", s)
    }
}

impl core::convert::From<&str> for StringBuf {
    // Required method
    fn from(value: &str) -> Self {
        let mut s = StringBuf([0u8; STRING_BUF_NBYTES]);
        s.0.iter_mut()
            .zip(value.as_bytes())
            .for_each(|(x, y)| *x = *y);
        s
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip() {
        let sb: StringBuf = "hello".into();
        println!("{}", sb);
    }
}
