use std::borrow::Borrow;
use std::borrow::BorrowMut;
use std::ffi::CStr;
use std::mem::MaybeUninit;

use nix::sys::socket::AddressFamily;
use nix::sys::socket::SockaddrLike;
use static_assertions::{assert_eq_align, assert_eq_size};

/// A container for any type of socket address.
#[derive(Clone, Copy)]
pub struct SockaddrStorage {
    addr: Addr,
    len: libc::socklen_t,
}

#[derive(Clone, Copy)]
#[repr(C)]
union Addr {
    slice: [MaybeUninit<u8>; std::mem::size_of::<libc::sockaddr_storage>()],
    storage: libc::sockaddr_storage,
    inet: libc::sockaddr_in,
    inet6: libc::sockaddr_in6,
    unix: libc::sockaddr_un,
}

// verify there are no larger fields larger than `libc::sockaddr_storage`
assert_eq_size!(libc::sockaddr_storage, Addr);

// NOTE: If any mutable methods are added to `SockaddrStorage` in the future, they should make sure
// that the address family cannot be changed. Otherwise a `SockaddrStorage` containing for example a
// `sockaddr_in` could be reinterpreted as another type such as `sockaddr_un`, the `sockaddr_in` may
// have uninitialized padding bytes in the location of a field of `sockaddr_un`, and a read of this
// field of `sockaddr_un` would then cause UB.

impl SockaddrStorage {
    /// # Safety
    ///
    /// - The address must be fully initialized, including padding fields (for example
    ///   `sockaddr_in.sin_zero`), up until `len` bytes.
    /// - Padding bytes do not need to be initialized.
    /// - The address does not need to be aligned.
    /// - If `len` is large enough for the address to hold the family field, the family must
    ///   correctly represent the address type. For example if `addr` points to a `sockaddr_in`,
    ///   then `addr.sin_family` must be `AF_INET`.
    pub unsafe fn from_ptr(
        addr: *const MaybeUninit<u8>,
        len: libc::socklen_t,
    ) -> Option<SockaddrStorage> {
        if addr.is_null() {
            return None;
        }

        const STORAGE_LEN: usize = std::mem::size_of::<libc::sockaddr_storage>();

        if (len as usize) > STORAGE_LEN {
            return None;
        }

        // 'new_addr' starts will all bytes initialized
        let mut new_addr = [MaybeUninit::new(0u8); STORAGE_LEN];

        // after the copy, 'new_addr' may have uninitialized bytes if `addr` had padding bytes
        unsafe { std::ptr::copy_nonoverlapping(addr, new_addr.as_mut_ptr(), len as usize) };

        Some(SockaddrStorage {
            addr: Addr { slice: new_addr },
            len,
        })
    }

    /// # Safety
    ///
    /// See [`Self::from_ptr`].
    pub unsafe fn from_bytes(address: &[MaybeUninit<u8>]) -> Option<Self> {
        unsafe { Self::from_ptr(address.as_ptr(), address.len().try_into().ok()?) }
    }

    /// Get the socket protocol family. Will return `None` if the socket address length is too
    /// short, or if the family value does not correspond to a valid/known family.
    pub fn family(&self) -> Option<AddressFamily> {
        if (self.len as usize) < memoffset::span_of!(libc::sockaddr_storage, ss_family).end {
            return None;
        }

        // SAFETY: we don't know what bytes of the address have initialized/uninitialized memory,
        // but we should be guarenteed the the `ss_family` field is initialized for any socket type.
        let family = unsafe { self.addr.storage }.ss_family;
        AddressFamily::from_i32(family.into())
    }

    /// If the socket address represents a valid ipv4 socket address (correct family and length),
    /// returns the ipv4 socket address.
    pub fn as_inet(&self) -> Option<&nix::sys::socket::SockaddrIn> {
        if (self.len as usize) < std::mem::size_of::<libc::sockaddr_in>() {
            return None;
        }
        if self.family() != Some(AddressFamily::Inet) {
            return None;
        }

        // SAFETY: Assume that `nix::sys::socket::SockaddrIn` is a transparent wrapper around a
        // `libc::sockaddr_in`. Verify (as best we can) that this is true.
        assert_eq_size!(libc::sockaddr_in, nix::sys::socket::SockaddrIn);
        assert_eq_align!(libc::sockaddr_in, nix::sys::socket::SockaddrIn);

        Some(unsafe {
            &*(std::ptr::from_ref(&self.addr.inet) as *const nix::sys::socket::SockaddrIn)
        })
    }

    /// Get a new `SockaddrStorage` with a copy of the ipv4 socket address.
    pub fn from_inet(addr: &nix::sys::socket::SockaddrIn) -> Self {
        // SAFETY: Assume that `nix::sys::socket::SockaddrIn` is a transparent wrapper around a
        // `libc::sockaddr_in`. Verify (as best we can) that this is true.
        assert_eq_size!(libc::sockaddr_in, nix::sys::socket::SockaddrIn);
        assert_eq_align!(libc::sockaddr_in, nix::sys::socket::SockaddrIn);

        unsafe { Self::from_ptr(addr.as_ptr() as *const MaybeUninit<u8>, addr.len()) }.unwrap()
    }

    /// If the socket address represents a valid ipv6 socket address (correct family and length),
    /// returns the ipv6 socket address.
    pub fn as_inet6(&self) -> Option<&nix::sys::socket::SockaddrIn6> {
        if (self.len as usize) < std::mem::size_of::<libc::sockaddr_in6>() {
            return None;
        }
        if self.family() != Some(AddressFamily::Inet6) {
            return None;
        }

        // SAFETY: Assume that `nix::sys::socket::SockaddrIn6` is a transparent wrapper around a
        // `libc::sockaddr_in6`. Verify (as best we can) that this is true.
        assert_eq_size!(libc::sockaddr_in6, nix::sys::socket::SockaddrIn6);
        assert_eq_align!(libc::sockaddr_in6, nix::sys::socket::SockaddrIn6);

        Some(unsafe {
            &*(std::ptr::from_ref(&self.addr.inet6) as *const nix::sys::socket::SockaddrIn6)
        })
    }

    /// Get a new `SockaddrStorage` with a copy of the ipv6 socket address.
    pub fn from_inet6(addr: &nix::sys::socket::SockaddrIn6) -> Self {
        // SAFETY: Assume that `nix::sys::socket::SockaddrIn6` is a transparent wrapper around a
        // `libc::sockaddr_in6`. Verify (as best we can) that this is true.
        assert_eq_size!(libc::sockaddr_in6, nix::sys::socket::SockaddrIn6);
        assert_eq_align!(libc::sockaddr_in6, nix::sys::socket::SockaddrIn6);

        unsafe { Self::from_ptr(addr.as_ptr() as *const MaybeUninit<u8>, addr.len()) }.unwrap()
    }

    /// If the socket address represents a valid unix socket address (correct family and length),
    /// returns the unix socket address.
    pub fn as_unix(&self) -> Option<SockaddrUnix<&libc::sockaddr_un>> {
        if self.family() != Some(AddressFamily::Unix) {
            return None;
        }

        SockaddrUnix::new(unsafe { &self.addr.unix }, self.len)
    }

    /// Get a new `SockaddrStorage` with a copy of the unix socket address.
    pub fn from_unix(addr: &SockaddrUnix<&libc::sockaddr_un>) -> Self {
        let (ptr, len) = addr.as_ptr();

        unsafe { Self::from_ptr(ptr as *const MaybeUninit<u8>, len) }.unwrap()
    }

    /// A pointer to the socket address. Some bytes may be uninitialized.
    pub fn as_ptr(&self) -> (*const MaybeUninit<u8>, libc::socklen_t) {
        (unsafe { &self.addr.slice }.as_ptr(), self.len)
    }

    /// The socket address as a slice of bytes. Some bytes may be uninitialized.
    pub fn as_slice(&self) -> &[MaybeUninit<u8>] {
        unsafe { &self.addr.slice[..(self.len as usize)] }
    }
}

impl std::fmt::Debug for SockaddrStorage {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let as_inet = self.as_inet();
        let as_inet6 = self.as_inet6();
        let as_unix = self.as_unix();

        let as_inet = as_inet.map(|x| x as &dyn std::fmt::Debug);
        let as_inet6 = as_inet6.map(|x| x as &dyn std::fmt::Debug);
        let as_unix = as_unix.as_ref().map(|x| x as &dyn std::fmt::Debug);

        // find a representation that is not None
        let options = [as_inet, as_inet6, as_unix];
        let addr = options.into_iter().find_map(std::convert::identity);

        if let Some(ref addr) = addr {
            f.debug_struct("SockaddrStorage")
                .field("len", &self.len)
                .field("addr", addr)
                .finish()
        } else {
            f.debug_struct("SockaddrStorage")
                .field("len", &self.len)
                .field("family", &self.family())
                .finish_non_exhaustive()
        }
    }
}

impl std::fmt::Display for SockaddrStorage {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let as_inet = self.as_inet();
        let as_inet6 = self.as_inet6();
        let as_unix = self.as_unix();

        let as_inet = as_inet.map(|x| x as &dyn std::fmt::Display);
        let as_inet6 = as_inet6.map(|x| x as &dyn std::fmt::Display);
        let as_unix = as_unix.as_ref().map(|x| x as &dyn std::fmt::Display);

        // find a representation that is not None
        let options = [as_inet, as_inet6, as_unix];
        let addr = options.into_iter().find_map(std::convert::identity);

        if let Some(ref addr) = addr {
            write!(f, "{addr}")
        } else {
            f.debug_struct("SockaddrStorage")
                .field("len", &self.len)
                .field("family", &self.family())
                .finish_non_exhaustive()
        }
    }
}

impl<T> From<SockaddrUnix<T>> for SockaddrStorage
where
    T: Borrow<libc::sockaddr_un>,
{
    fn from(addr: SockaddrUnix<T>) -> Self {
        SockaddrStorage::from_unix(&addr.as_ref())
    }
}

impl From<nix::sys::socket::SockaddrIn> for SockaddrStorage {
    fn from(addr: nix::sys::socket::SockaddrIn) -> Self {
        SockaddrStorage::from_inet(&addr)
    }
}

impl From<nix::sys::socket::SockaddrIn6> for SockaddrStorage {
    fn from(addr: nix::sys::socket::SockaddrIn6) -> Self {
        SockaddrStorage::from_inet6(&addr)
    }
}

impl From<std::net::SocketAddrV4> for SockaddrStorage {
    fn from(addr: std::net::SocketAddrV4) -> Self {
        nix::sys::socket::SockaddrIn::from(addr).into()
    }
}

impl From<std::net::SocketAddrV6> for SockaddrStorage {
    fn from(addr: std::net::SocketAddrV6) -> Self {
        nix::sys::socket::SockaddrIn6::from(addr).into()
    }
}

/// A Unix socket address. Typically will be used as an owned address
/// `SockaddrUnix<libc::sockaddr_un>` or a borrowed address `SockaddrUnix<&libc::sockaddr_un>`, and
/// you can convert between them using methods such as [`as_ref`](Self::as_ref) or
/// [`into_owned`](Self::into_owned).
#[derive(Clone, Copy)]
pub struct SockaddrUnix<T>
where
    T: Borrow<libc::sockaddr_un>,
{
    addr: T,
    len: libc::socklen_t,
}

impl<T> SockaddrUnix<T>
where
    T: Borrow<libc::sockaddr_un>,
{
    /// Get a new `SockaddrUnix` for a `libc::sockaddr_un`. The `libc::sockaddr_un` must be
    /// properly initialized. Will return `None` if the length is too short, too long, or if
    /// `sun_family` is not `AF_UNIX`.
    pub fn new(addr: T, len: libc::socklen_t) -> Option<Self> {
        if (len as usize) < memoffset::span_of!(libc::sockaddr_un, sun_family).end {
            return None;
        }

        if (len as usize) > std::mem::size_of::<libc::sockaddr_un>() {
            return None;
        }

        if addr.borrow().sun_family as i32 != libc::AF_UNIX {
            return None;
        }

        Some(Self { addr, len })
    }

    /// If the socket address represents a pathname address, returns the C string representing the
    /// filesystem path.
    pub fn as_path(&self) -> Option<&CStr> {
        let path = self.sun_path()?;

        // if the address length is too short, or it's an abstract named address
        if path.is_empty() || path[0] == 0 {
            return None;
        }

        // For pathname socket addresses, the path is a C-style nul-terminated string which may be
        // shorter than the address length (`self.len`). Bytes after the nul are ignored.
        CStr::from_bytes_until_nul(path).ok()
    }

    /// If the socket address represents an abstract address, returns the bytes representing the
    /// name of the abstract socket address. These bytes do not include the nul byte at
    /// `sun_path[0]`.
    pub fn as_abstract(&self) -> Option<&[u8]> {
        let name = self.sun_path()?;

        if name.is_empty() {
            return None;
        }

        // the first byte of `sun_path` is always 0 for abstract named socket addresses
        if name[0] != 0 {
            return None;
        }

        Some(&name[1..])
    }

    /// Is the unix socket address unnamed? On Linux, unnamed unix sockets are unnamed if their
    /// length is `size_of::<libc::sa_family_t>()`.
    pub fn is_unnamed(&self) -> bool {
        (self.len as usize) == memoffset::span_of!(libc::sockaddr_un, sun_family).end
    }

    /// Returns a slice with the valid bytes of `sun_path`, or `None` if the address length is too
    /// short.
    fn sun_path(&self) -> Option<&[u8]> {
        let path_offset = memoffset::offset_of!(libc::sockaddr_un, sun_path);
        let path_len = (self.len as usize).checked_sub(path_offset)?;

        Some(i8_to_u8_slice(&self.addr.borrow().sun_path[..path_len]))
    }

    /// Get an owned unix socket address.
    pub fn into_owned(self) -> SockaddrUnix<libc::sockaddr_un> {
        SockaddrUnix {
            addr: *self.addr.borrow(),
            len: self.len,
        }
    }

    /// Get a borrowed unix socket address.
    pub fn as_ref(&self) -> SockaddrUnix<&libc::sockaddr_un> {
        SockaddrUnix {
            addr: self.addr.borrow(),
            len: self.len,
        }
    }

    /// Get a pointer to the unix socket address. All fields of the `libc::sockaddr_un` will be
    /// properly initialized.
    pub fn as_ptr(&self) -> (*const libc::sockaddr_un, libc::socklen_t) {
        (self.addr.borrow(), self.len)
    }
}

impl<T> SockaddrUnix<T>
where
    T: BorrowMut<libc::sockaddr_un>,
{
    /// Get a mutably borrowed unix socket address.
    pub fn as_mut(&mut self) -> SockaddrUnix<&mut libc::sockaddr_un> {
        SockaddrUnix {
            addr: self.addr.borrow_mut(),
            len: self.len,
        }
    }
}

impl SockaddrUnix<libc::sockaddr_un> {
    /// Get a new `SockaddrUnix` with the given path. Will return `None` if the path is empty or is
    /// too large.
    pub fn new_path(path: &CStr) -> Option<Self> {
        let path = path.to_bytes();

        // this should be guaranteed by the CStr
        debug_assert!(!path.contains(&0));

        if path.is_empty() {
            // you cannot have a pathname unix socket address with no path
            return None;
        }

        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };

        if path.len() >= std::mem::size_of_val(&addr.sun_path) {
            // pathname unix sockets should be nul terminated, but there's no room for the nul
            return None;
        }

        // there will be a terminating nul byte since the address was zeroed, and the path will
        // never fill all of `sun_path`
        addr.sun_family = libc::AF_UNIX as u16;
        addr.sun_path[..path.len()].copy_from_slice(u8_to_i8_slice(path));

        let len = memoffset::offset_of!(libc::sockaddr_un, sun_path) + path.len() + 1;
        let len = len as libc::socklen_t;

        Some(Self { addr, len })
    }

    /// Get a new `SockaddrUnix` with the given abstract address name. The name does not include the
    /// required nul byte at `sun_path[0]`. Will return `None` if the name is too large.
    pub fn new_abstract(name: &[u8]) -> Option<Self> {
        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };

        if name.len() + 1 > std::mem::size_of_val(&addr.sun_path) {
            return None;
        }

        addr.sun_family = libc::AF_UNIX as u16;
        addr.sun_path[1..][..name.len()].copy_from_slice(u8_to_i8_slice(name));

        let len = memoffset::offset_of!(libc::sockaddr_un, sun_path) + 1 + name.len();
        let len = len as libc::socklen_t;

        Some(Self { addr, len })
    }

    /// Get a new unnamed unix socket address.
    pub fn new_unnamed() -> SockaddrUnix<libc::sockaddr_un> {
        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };
        addr.sun_family = libc::AF_UNIX as u16;

        let len = memoffset::span_of!(libc::sockaddr_un, sun_family).end;
        assert_eq!(len, 2);
        let len = len as libc::socklen_t;

        Self { addr, len }
    }
}

impl<T> std::fmt::Debug for SockaddrUnix<T>
where
    T: Borrow<libc::sockaddr_un>,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SockaddrUnix")
            .field("sun_family", &self.addr.borrow().sun_family)
            .field("sun_path", &self.sun_path())
            .finish()
    }
}

impl<T> std::fmt::Display for SockaddrUnix<T>
where
    T: Borrow<libc::sockaddr_un>,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(path) = self.as_path() {
            f.debug_struct("sockaddr_un").field("path", &path).finish()
        } else if let Some(name) = self.as_abstract() {
            let name: Vec<u8> = name
                .iter()
                .flat_map(|x| std::ascii::escape_default(*x))
                .collect();
            let name = String::from_utf8(name).unwrap();
            f.debug_struct("sockaddr_un")
                .field("abstract", &name)
                .finish()
        } else if self.is_unnamed() {
            write!(f, "sockaddr_un {{ unnamed }}")
        } else {
            f.debug_struct("sockaddr_un")
                .field("sun_path", &self.sun_path())
                .finish()
        }
    }
}

impl<T> PartialEq for SockaddrUnix<T>
where
    T: Borrow<libc::sockaddr_un>,
{
    fn eq(&self, other: &Self) -> bool {
        // if this assertion fails, something is very wrong
        assert_eq!(
            self.addr.borrow().sun_family,
            other.addr.borrow().sun_family,
        );
        self.len == other.len && self.sun_path() == other.sun_path()
    }
}

impl<T> Eq for SockaddrUnix<T> where T: Borrow<libc::sockaddr_un> {}

/// Convert a `&[u8]` to `&[i8]`.
fn u8_to_i8_slice(s: &[u8]) -> &[i8] {
    unsafe { std::slice::from_raw_parts(s.as_ptr() as *const i8, s.len()) }
}

/// Convert a `&[i8]` to `&[u8]`.
fn i8_to_u8_slice(s: &[i8]) -> &[u8] {
    unsafe { std::slice::from_raw_parts(s.as_ptr() as *const u8, s.len()) }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Convert from a `sockaddr_in` to a `SockaddrStorage`.
    #[test]
    fn storage_from_inet_ptr() {
        let mut addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
        addr.sin_family = libc::AF_INET as u16;
        addr.sin_port = 9000u16.to_be();
        addr.sin_addr = libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        };

        let ptr = std::ptr::from_ref(&addr) as *const MaybeUninit<u8>;
        let len = std::mem::size_of_val(&addr).try_into().unwrap();

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len) }.unwrap();

        assert_eq!(addr.family(), Some(AddressFamily::Inet));
        assert!(addr.as_inet().is_some());
        assert!(addr.as_inet6().is_none());
        assert!(addr.as_unix().is_none());
    }

    /// Convert from a `sockaddr_un` to a `SockaddrStorage`.
    #[test]
    fn storage_from_unix_ptr() {
        // a valid unix pathname sockaddr with a path of length 3
        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };
        addr.sun_family = libc::AF_UNIX as u16;
        addr.sun_path = [1; 108];
        addr.sun_path[..4].copy_from_slice(&[1, 2, 3, 0]);

        let ptr = std::ptr::from_ref(&addr) as *const MaybeUninit<u8>;
        let len = std::mem::size_of_val(&addr).try_into().unwrap();

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len) }.unwrap();

        assert_eq!(addr.family(), Some(AddressFamily::Unix));
        assert!(addr.as_unix().is_some());
        assert!(addr.as_inet().is_none());
        assert!(addr.as_inet6().is_none());
    }

    /// Convert from a `sockaddr_in` to a `SockaddrStorage` to a `SockaddrIn`.
    #[test]
    fn inet_addr_from_libc() {
        let mut addr_in: libc::sockaddr_in = unsafe { std::mem::zeroed() };
        addr_in.sin_family = libc::AF_INET as u16;
        addr_in.sin_port = 9000u16.to_be();
        addr_in.sin_addr = libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        };

        let ptr = std::ptr::from_ref(&addr_in) as *const MaybeUninit<u8>;
        let len = std::mem::size_of_val(&addr_in).try_into().unwrap();

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len) }.unwrap();
        let addr = addr.as_inet().unwrap();

        assert_eq!(addr.port(), u16::from_be(addr_in.sin_port));
        assert_eq!(addr.ip(), u32::from_be(addr_in.sin_addr.s_addr));
    }

    /// Convert from a `SockaddrIn` to a `SockaddrStorage` to a `sockaddr_in`.
    #[test]
    fn inet_addr_to_libc() {
        let addr_original = nix::sys::socket::SockaddrIn::new(127, 0, 0, 1, 9000);
        let addr = SockaddrStorage::from_inet(&addr_original);

        let (ptr, len) = addr.as_ptr();
        let ptr = ptr as *const libc::sockaddr_in;
        assert_eq!(len as usize, std::mem::size_of::<libc::sockaddr_in>());

        let addr = unsafe { ptr.as_ref() }.unwrap();

        assert_eq!(addr.sin_family, libc::AF_INET as u16);
        assert_eq!(u16::from_be(addr.sin_port), addr_original.port());
        assert_eq!(u32::from_be(addr.sin_addr.s_addr), addr_original.ip());
    }

    /// Convert from a pathname `sockaddr_un` to a `SockaddrStorage` to a `SockaddrUnix`.
    #[test]
    fn unix_addr_from_libc_to_path() {
        let pathname = [1, 2, 3, 0];
        let pathname_cstr = CStr::from_bytes_with_nul(i8_to_u8_slice(&pathname)).unwrap();

        // a valid unix pathname sockaddr with a path of length 3
        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };
        addr.sun_family = libc::AF_UNIX as u16;
        addr.sun_path = [1; 108];
        addr.sun_path[..pathname.len()].copy_from_slice(&pathname);

        let ptr = std::ptr::from_ref(&addr) as *const MaybeUninit<u8>;
        let len_useful_info = memoffset::offset_of!(libc::sockaddr_un, sun_path) + pathname.len();
        let len_useful_info = len_useful_info.try_into().unwrap();
        let len_struct = std::mem::size_of_val(&addr).try_into().unwrap();

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len_useful_info) }.unwrap();

        assert!(addr.as_inet().is_none());
        assert!(addr.as_inet6().is_none());

        let addr = addr.as_unix().unwrap();

        assert!(addr.as_abstract().is_none());
        assert!(!addr.is_unnamed());

        assert_eq!(addr.as_path().unwrap(), pathname_cstr);

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len_struct) }.unwrap();

        assert!(addr.as_inet().is_none());
        assert!(addr.as_inet6().is_none());

        let addr = addr.as_unix().unwrap();

        assert!(addr.as_abstract().is_none());
        assert!(!addr.is_unnamed());

        assert_eq!(addr.as_path().unwrap(), pathname_cstr);
    }

    /// Convert from a pathname `SockaddrUnix` to a `SockaddrStorage` to a `sockaddr_un`.
    #[test]
    fn unix_addr_from_path_to_libc() {
        let pathname = [1, 2, 3, 0];
        let pathname_cstr = CStr::from_bytes_with_nul(i8_to_u8_slice(&pathname)).unwrap();

        let addr = SockaddrUnix::new_path(pathname_cstr).unwrap();
        let addr = SockaddrStorage::from_unix(&addr.as_ref());

        let (ptr, len) = addr.as_ptr();
        let ptr = ptr as *const libc::sockaddr_un;
        assert_eq!(len as usize, 2 + pathname.len());

        let addr = unsafe { ptr.as_ref() }.unwrap();
        let path_len = len as usize - memoffset::offset_of!(libc::sockaddr_un, sun_path);

        assert_eq!(addr.sun_family, libc::AF_UNIX as u16);
        assert_eq!(addr.sun_path[..path_len], pathname);
    }

    /// Convert from a abstract-named `sockaddr_un` to a `SockaddrStorage` to a `SockaddrUnix`.
    #[test]
    fn unix_addr_from_libc_to_abstract() {
        let name = [1, 2, 3, 0, 5, 6];

        // a valid unix abstract sockaddr
        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };
        addr.sun_family = libc::AF_UNIX as u16;
        addr.sun_path = [1; 108];
        addr.sun_path[0] = 0;
        addr.sun_path[1..][..name.len()].copy_from_slice(u8_to_i8_slice(&name));

        let ptr = std::ptr::from_ref(&addr) as *const MaybeUninit<u8>;

        // the correct sockaddr length for this abstract unix socket address
        let len_real = memoffset::offset_of!(libc::sockaddr_un, sun_path) + 1 + name.len();
        let len_real = len_real.try_into().unwrap();

        // an incorrect sockaddr length (will result in the wrong abstract address name)
        let len_struct = std::mem::size_of_val(&addr).try_into().unwrap();

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len_real) }.unwrap();

        assert!(addr.as_inet().is_none());
        assert!(addr.as_inet6().is_none());

        let addr = addr.as_unix().unwrap();

        assert!(addr.as_path().is_none());
        assert!(!addr.is_unnamed());

        assert_eq!(addr.as_abstract().unwrap(), &name);

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len_struct) }.unwrap();

        assert!(addr.as_inet().is_none());
        assert!(addr.as_inet6().is_none());

        let addr = addr.as_unix().unwrap();

        assert!(addr.as_path().is_none());
        assert!(!addr.is_unnamed());

        assert_eq!(addr.as_abstract().unwrap().len(), 107);
    }

    /// Convert from an abstract-named `SockaddrUnix` to a `SockaddrStorage` to a `sockaddr_un`.
    #[test]
    fn unix_addr_from_abstract_to_libc() {
        let name = [1, 2, 3, 0, 5, 6];

        let addr = SockaddrUnix::new_abstract(&name).unwrap();
        let addr = SockaddrStorage::from_unix(&addr.as_ref());

        let (ptr, len) = addr.as_ptr();
        let ptr = ptr as *const libc::sockaddr_un;
        assert_eq!(len as usize, 2 + 1 + name.len());

        let addr = unsafe { ptr.as_ref() }.unwrap();
        let path_len = len as usize - memoffset::offset_of!(libc::sockaddr_un, sun_path);

        assert_eq!(addr.sun_family, libc::AF_UNIX as u16);
        assert_eq!(addr.sun_path[0], 0);
        assert_eq!(&addr.sun_path[1..path_len], u8_to_i8_slice(&name));
    }

    /// Convert from an unnamed `sockaddr_un` to a `SockaddrStorage` to a `SockaddrUnix`.
    #[test]
    fn unix_addr_from_libc_to_unnamed() {
        // a valid unix abstract sockaddr
        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };
        addr.sun_family = libc::AF_UNIX as u16;
        addr.sun_path = [1; 108];

        let ptr = std::ptr::from_ref(&addr) as *const MaybeUninit<u8>;

        // the correct sockaddr length for this unnamed unix socket address
        let len_real = memoffset::offset_of!(libc::sockaddr_un, sun_path);
        let len_real = len_real.try_into().unwrap();

        // an incorrect sockaddr length (will result in the wrong address)
        let len_struct = std::mem::size_of_val(&addr).try_into().unwrap();

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len_real) }.unwrap();

        assert!(addr.as_inet().is_none());
        assert!(addr.as_inet6().is_none());

        let addr = addr.as_unix().unwrap();

        assert!(addr.is_unnamed());
        assert!(addr.as_path().is_none());
        assert!(addr.as_abstract().is_none());

        let addr = unsafe { SockaddrStorage::from_ptr(ptr, len_struct) }.unwrap();

        assert!(addr.as_inet().is_none());
        assert!(addr.as_inet6().is_none());

        let addr = addr.as_unix().unwrap();

        // the sun_path value isn't a valid pathname or abstract address, and it's not unnamed
        // because of the length (len_struct > 2)
        assert!(!addr.is_unnamed());
        assert!(addr.as_abstract().is_none());
        assert!(addr.as_path().is_none());
    }

    /// Convert from an unnamed `SockaddrUnix` to a `SockaddrStorage` to a `sockaddr_un`.
    #[test]
    fn unix_addr_from_unnamed_to_libc() {
        let addr = SockaddrUnix::new_unnamed();
        let addr = SockaddrStorage::from_unix(&addr.as_ref());

        let (ptr, len) = addr.as_ptr();
        let ptr = ptr as *const libc::sockaddr_un;
        assert_eq!(len, 2);

        let addr = unsafe { ptr.as_ref() }.unwrap();

        assert_eq!(addr.sun_family, libc::AF_UNIX as u16);
    }
}
