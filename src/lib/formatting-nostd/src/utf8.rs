const UNICODE_REPLACEMENT_CHAR_STR: &str = "�";

/// Split buffer after the first valid utf8 character.
/// Returns an error if `buf` does not start with a valid utf8 character.
///
/// Panics if `buf` is empty.
///
/// ```
/// # use formatting_nostd::utf8::split_at_first_char;
/// assert_eq!(split_at_first_char(
///   &['1' as u8, '2' as u8, '3' as u8][..]),
///   Some(("1", &['2' as u8, '3' as u8][..])));
/// assert_eq!(split_at_first_char(
///   &[0x80, '2' as u8, '3' as u8][..]),
///   None);
/// ```
pub fn split_at_first_char(buf: &[u8]) -> Option<(&str, &[u8])> {
    assert!(!buf.is_empty());
    for charlen in 1..=core::cmp::min(4, buf.len()) {
        if let Ok(s) = core::str::from_utf8(&buf[..charlen]) {
            return Some((s, &buf[charlen..]));
        }
    }
    None
}

#[cfg(test)]
#[test]
fn test_split_at_first_char() {
    // Valid first char; multiple lengths
    assert_eq!(
        split_at_first_char(&[b'1', b'2', b'3'][..]),
        Some(("1", &[b'2', b'3'][..]))
    );
    assert_eq!(
        split_at_first_char(&[b'1', b'2'][..]),
        Some(("1", &[b'2'][..]))
    );
    assert_eq!(split_at_first_char(&[b'1'][..]), Some(("1", &[][..])));

    // Invalid first char; multiple lengths
    assert_eq!(split_at_first_char(&[0x80, b'2', b'3'][..]), None);
    assert_eq!(split_at_first_char(&[0x80, b'2'][..]), None);
    assert_eq!(split_at_first_char(&[0x80][..]), None);

    // > 1-byte first characters
    assert_eq!(
        split_at_first_char(&[0xc2, 0xa1, 0][..]),
        Some(("¡", &[0][..]))
    );
    assert_eq!(
        split_at_first_char(&[0xe0, 0xa4, 0xb9, 0][..]),
        Some(("ह", &[0][..]))
    );
    assert_eq!(
        split_at_first_char(&[0xf0, 0x90, 0x8d, 0x88, 0][..]),
        Some(("𐍈", &[0][..]))
    );
}

/// Split buffer after the first valid utf8 character.
///
/// If `buf` doesn't start with a valid utf8 character,
/// returns the unicode replacement character and the buffer
/// after skipping invalid bytes.
///
/// Panics if `buf` is empty.
///
/// ```
/// # use formatting_nostd::utf8::split_at_first_char_lossy;
/// assert_eq!(split_at_first_char_lossy(
///   &['1' as u8, '2' as u8, '3' as u8][..]),
///   ("1", &['2' as u8, '3' as u8][..]));
/// assert_eq!(split_at_first_char_lossy(
///   &[0x80, 0x80, '2' as u8, '3' as u8][..]),
///   ("�", &['2' as u8, '3' as u8][..]));
/// ```
pub fn split_at_first_char_lossy(mut buf: &[u8]) -> (&str, &[u8]) {
    assert!(!buf.is_empty());
    let mut invalid_seq = false;
    loop {
        let res = split_at_first_char(buf);
        if let Some((first_char, therest)) = res {
            return if invalid_seq {
                // We're at the end of an invalid sequence.
                (UNICODE_REPLACEMENT_CHAR_STR, buf)
            } else {
                (first_char, therest)
            };
        }
        // We're in an invalid sequence.
        invalid_seq = true;

        // Move forward one byte
        buf = &buf[1..];

        if buf.is_empty() {
            return (UNICODE_REPLACEMENT_CHAR_STR, buf);
        }
    }
}

#[cfg(test)]
#[test]
fn test_split_at_first_char_lossy() {
    // Using the implementatin knowledge that this is implemented using `split_at_first_char`;
    // just focus on testing the cases that are different for this method.
    assert_eq!(
        split_at_first_char_lossy(&[b'1', 2, 3][..]),
        ("1", &[2, 3][..])
    );
    assert_eq!(
        split_at_first_char_lossy(&[0x80, 2, 3][..]),
        (UNICODE_REPLACEMENT_CHAR_STR, &[2, 3][..])
    );
    assert_eq!(
        split_at_first_char_lossy(&[0x80, 0x80, 2, 3][..]),
        (UNICODE_REPLACEMENT_CHAR_STR, &[2, 3][..])
    );
}

pub struct DecodeLossyIterator<'a> {
    bytes: &'a [u8],
}

impl<'a> core::iter::Iterator for DecodeLossyIterator<'a> {
    type Item = &'a str;

    fn next(&mut self) -> Option<Self::Item> {
        if self.bytes.is_empty() {
            return None;
        }
        let (item, next_bytes) = split_at_first_char_lossy(self.bytes);
        self.bytes = next_bytes;
        Some(item)
    }
}

pub fn decode_lossy(bytes: &[u8]) -> DecodeLossyIterator<'_> {
    DecodeLossyIterator { bytes }
}

#[cfg(test)]
#[test]
fn test_lossy_decode_iterator() {
    assert_eq!(
        decode_lossy("123".as_bytes()).collect::<Vec<_>>(),
        vec!["1", "2", "3"]
    );
    assert_eq!(
        decode_lossy(&[0x80, 0x80, b'x', 0x80]).collect::<Vec<_>>(),
        vec![
            UNICODE_REPLACEMENT_CHAR_STR,
            "x",
            UNICODE_REPLACEMENT_CHAR_STR
        ]
    );
}
