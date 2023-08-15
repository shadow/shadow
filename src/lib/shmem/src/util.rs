// The standard path length limit on Linux.
pub const PATH_MAX_NBYTES: usize = 255;

// One extra byte for the null terminator.
pub(crate) type PathBuf = [u8; PATH_MAX_NBYTES + 1];

pub(crate) const NULL_PATH_BUF: PathBuf = [0; PATH_MAX_NBYTES + 1];

// A path is up to 256 bytes; an isize is 20 bytes; and one byte for delimiter.
// pub const SERIALIZED_BLOCK_BUF_NBYTES: usize = PATH_MAX_NBYTES + 20 + 1;

// One extra byte for the null terminator.
//pub(crate) type SerializedBlockBuf = [u8; SERIALIZED_BLOCK_BUF_NBYTES + 1];

//pub (crate) const NULL_SERIALIZED_BUF: SerializedBlockBuf = [0; SERIALIZED_BLOCK_BUF_NBYTES + 1];

pub(crate) fn trim_null_bytes<const N: usize>(s: &[u8; N]) -> Option<&[u8]> {
    if let Some(i) = s.iter().position(|x| *x == 0) {
        Some(&s[0..(i)])
    } else {
        None
    }
}

pub(crate) fn buf_from_utf8_str<const N: usize>(s: &str) -> Option<[u8; N]> {
    let mut retval = [0; N];

    if s.len() >= N {
        None
    } else {
        retval
            .iter_mut()
            .zip(s.as_bytes().iter())
            .for_each(|(x, y)| *x = *y);
        Some(retval)
    }
}
