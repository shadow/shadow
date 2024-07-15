use crate::bindings;

#[derive(Copy, Clone, PartialEq, Eq)]
// Defined in libc as an `int`, but u16 is sufficient
// to represent all values, and is what is used in `linux_errno`.
//
// We want to allow unknown values, but only up to Errno::MAX, making
// it difficult to use an `enum` here. e.g. if we used `num_enum` with a `catch_all` `Unknown`
// variant, we wouldn't be able to prevent construction of `Errno::Unknown(Errno::MAX.into() + 1)`.
pub struct Errno(u16);

impl TryFrom<u16> for Errno {
    type Error = ();

    fn try_from(val: u16) -> Result<Self, Self::Error> {
        Self::from_u16(val).ok_or(())
    }
}

impl TryFrom<u32> for Errno {
    type Error = ();

    fn try_from(val: u32) -> Result<Self, Self::Error> {
        u16::try_from(val).ok().and_then(Self::from_u16).ok_or(())
    }
}

impl TryFrom<u64> for Errno {
    type Error = ();

    fn try_from(val: u64) -> Result<Self, Self::Error> {
        u16::try_from(val).ok().and_then(Self::from_u16).ok_or(())
    }
}

impl TryFrom<i16> for Errno {
    type Error = ();

    fn try_from(val: i16) -> Result<Self, Self::Error> {
        u16::try_from(val).ok().and_then(Self::from_u16).ok_or(())
    }
}

impl TryFrom<i32> for Errno {
    type Error = ();

    fn try_from(val: i32) -> Result<Self, Self::Error> {
        u16::try_from(val).ok().and_then(Self::from_u16).ok_or(())
    }
}

impl TryFrom<i64> for Errno {
    type Error = ();

    fn try_from(val: i64) -> Result<Self, Self::Error> {
        u16::try_from(val).ok().and_then(Self::from_u16).ok_or(())
    }
}

impl From<Errno> for u16 {
    #[inline]
    fn from(val: Errno) -> u16 {
        val.0
    }
}

impl From<Errno> for u32 {
    #[inline]
    fn from(val: Errno) -> u32 {
        val.0.into()
    }
}

impl From<Errno> for u64 {
    #[inline]
    fn from(val: Errno) -> u64 {
        val.0.into()
    }
}

impl From<Errno> for i32 {
    #[inline]
    fn from(val: Errno) -> i32 {
        val.0.into()
    }
}

impl From<Errno> for i64 {
    #[inline]
    fn from(val: Errno) -> i64 {
        val.0.into()
    }
}

const fn errno_to_str(e: Errno) -> Option<&'static str> {
    match e {
        Errno::EINVAL => Some("EINVAL"),
        Errno::EDEADLK => Some("EDEADLK"),
        Errno::ENAMETOOLONG => Some("ENAMETOOLONG"),
        Errno::ENOLCK => Some("ENOLCK"),
        Errno::ENOSYS => Some("ENOSYS"),
        Errno::ENOTEMPTY => Some("ENOTEMPTY"),
        Errno::ELOOP => Some("ELOOP"),
        Errno::EWOULDBLOCK => Some("EWOULDBLOCK"),
        Errno::ENOMSG => Some("ENOMSG"),
        Errno::EIDRM => Some("EIDRM"),
        Errno::ECHRNG => Some("ECHRNG"),
        Errno::EL2NSYNC => Some("EL2NSYNC"),
        Errno::EL3HLT => Some("EL3HLT"),
        Errno::EL3RST => Some("EL3RST"),
        Errno::ELNRNG => Some("ELNRNG"),
        Errno::EUNATCH => Some("EUNATCH"),
        Errno::ENOCSI => Some("ENOCSI"),
        Errno::EL2HLT => Some("EL2HLT"),
        Errno::EBADE => Some("EBADE"),
        Errno::EBADR => Some("EBADR"),
        Errno::EXFULL => Some("EXFULL"),
        Errno::ENOANO => Some("ENOANO"),
        Errno::EBADRQC => Some("EBADRQC"),
        Errno::EBADSLT => Some("EBADSLT"),
        Errno::EBFONT => Some("EBFONT"),
        Errno::ENOSTR => Some("ENOSTR"),
        Errno::ENODATA => Some("ENODATA"),
        Errno::ETIME => Some("ETIME"),
        Errno::ENOSR => Some("ENOSR"),
        Errno::ENONET => Some("ENONET"),
        Errno::ENOPKG => Some("ENOPKG"),
        Errno::EREMOTE => Some("EREMOTE"),
        Errno::ENOLINK => Some("ENOLINK"),
        Errno::EADV => Some("EADV"),
        Errno::ESRMNT => Some("ESRMNT"),
        Errno::ECOMM => Some("ECOMM"),
        Errno::EPROTO => Some("EPROTO"),
        Errno::EMULTIHOP => Some("EMULTIHOP"),
        Errno::EDOTDOT => Some("EDOTDOT"),
        Errno::EBADMSG => Some("EBADMSG"),
        Errno::EOVERFLOW => Some("EOVERFLOW"),
        Errno::ENOTUNIQ => Some("ENOTUNIQ"),
        Errno::EBADFD => Some("EBADFD"),
        Errno::EREMCHG => Some("EREMCHG"),
        Errno::ELIBACC => Some("ELIBACC"),
        Errno::ELIBBAD => Some("ELIBBAD"),
        Errno::ELIBSCN => Some("ELIBSCN"),
        Errno::ELIBMAX => Some("ELIBMAX"),
        Errno::ELIBEXEC => Some("ELIBEXEC"),
        Errno::EILSEQ => Some("EILSEQ"),
        Errno::ERESTART => Some("ERESTART"),
        Errno::ESTRPIPE => Some("ESTRPIPE"),
        Errno::EUSERS => Some("EUSERS"),
        Errno::ENOTSOCK => Some("ENOTSOCK"),
        Errno::EDESTADDRREQ => Some("EDESTADDRREQ"),
        Errno::EMSGSIZE => Some("EMSGSIZE"),
        Errno::EPROTOTYPE => Some("EPROTOTYPE"),
        Errno::ENOPROTOOPT => Some("ENOPROTOOPT"),
        Errno::EPROTONOSUPPORT => Some("EPROTONOSUPPORT"),
        Errno::ESOCKTNOSUPPORT => Some("ESOCKTNOSUPPORT"),
        Errno::EOPNOTSUPP => Some("EOPNOTSUPP"),
        Errno::EPFNOSUPPORT => Some("EPFNOSUPPORT"),
        Errno::EAFNOSUPPORT => Some("EAFNOSUPPORT"),
        Errno::EADDRINUSE => Some("EADDRINUSE"),
        Errno::EADDRNOTAVAIL => Some("EADDRNOTAVAIL"),
        Errno::ENETDOWN => Some("ENETDOWN"),
        Errno::ENETUNREACH => Some("ENETUNREACH"),
        Errno::ENETRESET => Some("ENETRESET"),
        Errno::ECONNABORTED => Some("ECONNABORTED"),
        Errno::ECONNRESET => Some("ECONNRESET"),
        Errno::ENOBUFS => Some("ENOBUFS"),
        Errno::EISCONN => Some("EISCONN"),
        Errno::ENOTCONN => Some("ENOTCONN"),
        Errno::ESHUTDOWN => Some("ESHUTDOWN"),
        Errno::ETOOMANYREFS => Some("ETOOMANYREFS"),
        Errno::ETIMEDOUT => Some("ETIMEDOUT"),
        Errno::ECONNREFUSED => Some("ECONNREFUSED"),
        Errno::EHOSTDOWN => Some("EHOSTDOWN"),
        Errno::EHOSTUNREACH => Some("EHOSTUNREACH"),
        Errno::EALREADY => Some("EALREADY"),
        Errno::EINPROGRESS => Some("EINPROGRESS"),
        Errno::ESTALE => Some("ESTALE"),
        Errno::EUCLEAN => Some("EUCLEAN"),
        Errno::ENOTNAM => Some("ENOTNAM"),
        Errno::ENAVAIL => Some("ENAVAIL"),
        Errno::EISNAM => Some("EISNAM"),
        Errno::EREMOTEIO => Some("EREMOTEIO"),
        Errno::EDQUOT => Some("EDQUOT"),
        Errno::ENOMEDIUM => Some("ENOMEDIUM"),
        Errno::EMEDIUMTYPE => Some("EMEDIUMTYPE"),
        Errno::ECANCELED => Some("ECANCELED"),
        Errno::ENOKEY => Some("ENOKEY"),
        Errno::EKEYEXPIRED => Some("EKEYEXPIRED"),
        Errno::EKEYREVOKED => Some("EKEYREVOKED"),
        Errno::EKEYREJECTED => Some("EKEYREJECTED"),
        Errno::EOWNERDEAD => Some("EOWNERDEAD"),
        Errno::ENOTRECOVERABLE => Some("ENOTRECOVERABLE"),
        Errno::ERFKILL => Some("ERFKILL"),
        Errno::EHWPOISON => Some("EHWPOISON"),
        Errno::EINTR => Some("EINTR"),
        Errno::ENFILE => Some("ENFILE"),
        Errno::EPIPE => Some("EPIPE"),
        Errno::ESPIPE => Some("ESPIPE"),
        Errno::EBADF => Some("EBADF"),
        Errno::EPERM => Some("EPERM"),
        Errno::EFAULT => Some("EFAULT"),
        Errno::ESRCH => Some("ESRCH"),
        Errno::ENOENT => Some("ENOENT"),
        Errno::ENOTTY => Some("ENOTTY"),
        Errno::EEXIST => Some("EEXIST"),
        Errno::ECHILD => Some("ECHILD"),
        Errno::EACCES => Some("EACCES"),
        Errno::ENOEXEC => Some("ENOEXEC"),
        Errno::ENOTDIR => Some("ENOTDIR"),
        _ => None,
    }
}

impl core::fmt::Debug for Errno {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match errno_to_str(*self) {
            Some(s) => {
                formatter.write_str("Errno::")?;
                formatter.write_str(s)
            }
            None => write!(formatter, "Errno::<{}>", u16::from(*self)),
        }
    }
}

impl core::fmt::Display for Errno {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match errno_to_str(*self) {
            Some(s) => formatter.write_str(s),
            None => write!(formatter, "(unknown errno {})", u16::from(*self)),
        }
    }
}

impl Errno {
    pub const EINVAL: Self = Self::from_u32_const(bindings::LINUX_EINVAL);
    pub const EDEADLK: Self = Self::from_u32_const(bindings::LINUX_EDEADLK);
    pub const ENAMETOOLONG: Self = Self::from_u32_const(bindings::LINUX_ENAMETOOLONG);
    pub const ENOLCK: Self = Self::from_u32_const(bindings::LINUX_ENOLCK);
    pub const ENOSYS: Self = Self::from_u32_const(bindings::LINUX_ENOSYS);
    pub const ENOTEMPTY: Self = Self::from_u32_const(bindings::LINUX_ENOTEMPTY);
    pub const ELOOP: Self = Self::from_u32_const(bindings::LINUX_ELOOP);
    pub const EWOULDBLOCK: Self = Self::from_u32_const(bindings::LINUX_EWOULDBLOCK);
    pub const ENOMSG: Self = Self::from_u32_const(bindings::LINUX_ENOMSG);
    pub const EIDRM: Self = Self::from_u32_const(bindings::LINUX_EIDRM);
    pub const ECHRNG: Self = Self::from_u32_const(bindings::LINUX_ECHRNG);
    pub const EL2NSYNC: Self = Self::from_u32_const(bindings::LINUX_EL2NSYNC);
    pub const EL3HLT: Self = Self::from_u32_const(bindings::LINUX_EL3HLT);
    pub const EL3RST: Self = Self::from_u32_const(bindings::LINUX_EL3RST);
    pub const ELNRNG: Self = Self::from_u32_const(bindings::LINUX_ELNRNG);
    pub const EUNATCH: Self = Self::from_u32_const(bindings::LINUX_EUNATCH);
    pub const ENOCSI: Self = Self::from_u32_const(bindings::LINUX_ENOCSI);
    pub const EL2HLT: Self = Self::from_u32_const(bindings::LINUX_EL2HLT);
    pub const EBADE: Self = Self::from_u32_const(bindings::LINUX_EBADE);
    pub const EBADR: Self = Self::from_u32_const(bindings::LINUX_EBADR);
    pub const EXFULL: Self = Self::from_u32_const(bindings::LINUX_EXFULL);
    pub const ENOANO: Self = Self::from_u32_const(bindings::LINUX_ENOANO);
    pub const EBADRQC: Self = Self::from_u32_const(bindings::LINUX_EBADRQC);
    pub const EBADSLT: Self = Self::from_u32_const(bindings::LINUX_EBADSLT);
    pub const EBFONT: Self = Self::from_u32_const(bindings::LINUX_EBFONT);
    pub const ENOSTR: Self = Self::from_u32_const(bindings::LINUX_ENOSTR);
    pub const ENODATA: Self = Self::from_u32_const(bindings::LINUX_ENODATA);
    pub const ETIME: Self = Self::from_u32_const(bindings::LINUX_ETIME);
    pub const ENOSR: Self = Self::from_u32_const(bindings::LINUX_ENOSR);
    pub const ENONET: Self = Self::from_u32_const(bindings::LINUX_ENONET);
    pub const ENOPKG: Self = Self::from_u32_const(bindings::LINUX_ENOPKG);
    pub const EREMOTE: Self = Self::from_u32_const(bindings::LINUX_EREMOTE);
    pub const ENOLINK: Self = Self::from_u32_const(bindings::LINUX_ENOLINK);
    pub const EADV: Self = Self::from_u32_const(bindings::LINUX_EADV);
    pub const ESRMNT: Self = Self::from_u32_const(bindings::LINUX_ESRMNT);
    pub const ECOMM: Self = Self::from_u32_const(bindings::LINUX_ECOMM);
    pub const EPROTO: Self = Self::from_u32_const(bindings::LINUX_EPROTO);
    pub const EMULTIHOP: Self = Self::from_u32_const(bindings::LINUX_EMULTIHOP);
    pub const EDOTDOT: Self = Self::from_u32_const(bindings::LINUX_EDOTDOT);
    pub const EBADMSG: Self = Self::from_u32_const(bindings::LINUX_EBADMSG);
    pub const EOVERFLOW: Self = Self::from_u32_const(bindings::LINUX_EOVERFLOW);
    pub const ENOTUNIQ: Self = Self::from_u32_const(bindings::LINUX_ENOTUNIQ);
    pub const EBADFD: Self = Self::from_u32_const(bindings::LINUX_EBADFD);
    pub const EREMCHG: Self = Self::from_u32_const(bindings::LINUX_EREMCHG);
    pub const ELIBACC: Self = Self::from_u32_const(bindings::LINUX_ELIBACC);
    pub const ELIBBAD: Self = Self::from_u32_const(bindings::LINUX_ELIBBAD);
    pub const ELIBSCN: Self = Self::from_u32_const(bindings::LINUX_ELIBSCN);
    pub const ELIBMAX: Self = Self::from_u32_const(bindings::LINUX_ELIBMAX);
    pub const ELIBEXEC: Self = Self::from_u32_const(bindings::LINUX_ELIBEXEC);
    pub const EILSEQ: Self = Self::from_u32_const(bindings::LINUX_EILSEQ);
    pub const ERESTART: Self = Self::from_u32_const(bindings::LINUX_ERESTART);
    pub const ESTRPIPE: Self = Self::from_u32_const(bindings::LINUX_ESTRPIPE);
    pub const EUSERS: Self = Self::from_u32_const(bindings::LINUX_EUSERS);
    pub const ENOTSOCK: Self = Self::from_u32_const(bindings::LINUX_ENOTSOCK);
    pub const EDESTADDRREQ: Self = Self::from_u32_const(bindings::LINUX_EDESTADDRREQ);
    pub const EMSGSIZE: Self = Self::from_u32_const(bindings::LINUX_EMSGSIZE);
    pub const EPROTOTYPE: Self = Self::from_u32_const(bindings::LINUX_EPROTOTYPE);
    pub const ENOPROTOOPT: Self = Self::from_u32_const(bindings::LINUX_ENOPROTOOPT);
    pub const EPROTONOSUPPORT: Self = Self::from_u32_const(bindings::LINUX_EPROTONOSUPPORT);
    pub const ESOCKTNOSUPPORT: Self = Self::from_u32_const(bindings::LINUX_ESOCKTNOSUPPORT);
    pub const EOPNOTSUPP: Self = Self::from_u32_const(bindings::LINUX_EOPNOTSUPP);
    pub const EPFNOSUPPORT: Self = Self::from_u32_const(bindings::LINUX_EPFNOSUPPORT);
    pub const EAFNOSUPPORT: Self = Self::from_u32_const(bindings::LINUX_EAFNOSUPPORT);
    pub const EADDRINUSE: Self = Self::from_u32_const(bindings::LINUX_EADDRINUSE);
    pub const EADDRNOTAVAIL: Self = Self::from_u32_const(bindings::LINUX_EADDRNOTAVAIL);
    pub const ENETDOWN: Self = Self::from_u32_const(bindings::LINUX_ENETDOWN);
    pub const ENETUNREACH: Self = Self::from_u32_const(bindings::LINUX_ENETUNREACH);
    pub const ENETRESET: Self = Self::from_u32_const(bindings::LINUX_ENETRESET);
    pub const ECONNABORTED: Self = Self::from_u32_const(bindings::LINUX_ECONNABORTED);
    pub const ECONNRESET: Self = Self::from_u32_const(bindings::LINUX_ECONNRESET);
    pub const ENOBUFS: Self = Self::from_u32_const(bindings::LINUX_ENOBUFS);
    pub const EISCONN: Self = Self::from_u32_const(bindings::LINUX_EISCONN);
    pub const ENOTCONN: Self = Self::from_u32_const(bindings::LINUX_ENOTCONN);
    pub const ESHUTDOWN: Self = Self::from_u32_const(bindings::LINUX_ESHUTDOWN);
    pub const ETOOMANYREFS: Self = Self::from_u32_const(bindings::LINUX_ETOOMANYREFS);
    pub const ETIMEDOUT: Self = Self::from_u32_const(bindings::LINUX_ETIMEDOUT);
    pub const ECONNREFUSED: Self = Self::from_u32_const(bindings::LINUX_ECONNREFUSED);
    pub const EHOSTDOWN: Self = Self::from_u32_const(bindings::LINUX_EHOSTDOWN);
    pub const EHOSTUNREACH: Self = Self::from_u32_const(bindings::LINUX_EHOSTUNREACH);
    pub const EALREADY: Self = Self::from_u32_const(bindings::LINUX_EALREADY);
    pub const EINPROGRESS: Self = Self::from_u32_const(bindings::LINUX_EINPROGRESS);
    pub const ESTALE: Self = Self::from_u32_const(bindings::LINUX_ESTALE);
    pub const EUCLEAN: Self = Self::from_u32_const(bindings::LINUX_EUCLEAN);
    pub const ENOTNAM: Self = Self::from_u32_const(bindings::LINUX_ENOTNAM);
    pub const ENAVAIL: Self = Self::from_u32_const(bindings::LINUX_ENAVAIL);
    pub const EISNAM: Self = Self::from_u32_const(bindings::LINUX_EISNAM);
    pub const EREMOTEIO: Self = Self::from_u32_const(bindings::LINUX_EREMOTEIO);
    pub const EDQUOT: Self = Self::from_u32_const(bindings::LINUX_EDQUOT);
    pub const ENOMEDIUM: Self = Self::from_u32_const(bindings::LINUX_ENOMEDIUM);
    pub const EMEDIUMTYPE: Self = Self::from_u32_const(bindings::LINUX_EMEDIUMTYPE);
    pub const ECANCELED: Self = Self::from_u32_const(bindings::LINUX_ECANCELED);
    pub const ENOKEY: Self = Self::from_u32_const(bindings::LINUX_ENOKEY);
    pub const EKEYEXPIRED: Self = Self::from_u32_const(bindings::LINUX_EKEYEXPIRED);
    pub const EKEYREVOKED: Self = Self::from_u32_const(bindings::LINUX_EKEYREVOKED);
    pub const EKEYREJECTED: Self = Self::from_u32_const(bindings::LINUX_EKEYREJECTED);
    pub const EOWNERDEAD: Self = Self::from_u32_const(bindings::LINUX_EOWNERDEAD);
    pub const ENOTRECOVERABLE: Self = Self::from_u32_const(bindings::LINUX_ENOTRECOVERABLE);
    pub const ERFKILL: Self = Self::from_u32_const(bindings::LINUX_ERFKILL);
    pub const EHWPOISON: Self = Self::from_u32_const(bindings::LINUX_EHWPOISON);
    pub const EINTR: Self = Self::from_u32_const(bindings::LINUX_EINTR);
    pub const ENFILE: Self = Self::from_u32_const(bindings::LINUX_ENFILE);
    pub const EPIPE: Self = Self::from_u32_const(bindings::LINUX_EPIPE);
    pub const ESPIPE: Self = Self::from_u32_const(bindings::LINUX_ESPIPE);
    pub const EBADF: Self = Self::from_u32_const(bindings::LINUX_EBADF);
    pub const EPERM: Self = Self::from_u32_const(bindings::LINUX_EPERM);
    pub const EFAULT: Self = Self::from_u32_const(bindings::LINUX_EFAULT);
    pub const ESRCH: Self = Self::from_u32_const(bindings::LINUX_ESRCH);
    pub const ENOENT: Self = Self::from_u32_const(bindings::LINUX_ENOENT);
    pub const ENOTTY: Self = Self::from_u32_const(bindings::LINUX_ENOTTY);
    pub const EEXIST: Self = Self::from_u32_const(bindings::LINUX_EEXIST);
    pub const ECHILD: Self = Self::from_u32_const(bindings::LINUX_ECHILD);
    pub const EACCES: Self = Self::from_u32_const(bindings::LINUX_EACCES);
    pub const ENOEXEC: Self = Self::from_u32_const(bindings::LINUX_ENOEXEC);
    pub const ENOTDIR: Self = Self::from_u32_const(bindings::LINUX_ENOTDIR);
    // NOTE: add new entries to `errno_to_str` above

    // Aliases
    pub const EDEADLOCK: Self = Self::from_u32_const(bindings::LINUX_EDEADLOCK);
    pub const EAGAIN: Self = Self::from_u32_const(bindings::LINUX_EAGAIN);
    pub const ENOTSUP: Self = Self::EOPNOTSUPP;

    /// From MAX_ERRNO in include/linux/err.h in kernel source. This doesn't
    /// seem to be exposed in the installed kernel headers from which we generate bindings.
    /// <https://github.com/torvalds/linux/blob/a4d7d701121981e3c3fe69ade376fe9f26324161/include/linux/err.h#L18>
    pub const MAX: Self = Self(4095);

    #[inline]
    pub const fn from_u16(val: u16) -> Option<Self> {
        const MAX: u16 = Errno::MAX.0;
        match val {
            1..=MAX => Some(Self(val)),
            _ => None,
        }
    }

    /// For C interop.
    #[inline]
    pub const fn to_negated_i64(self) -> i64 {
        let val: u16 = self.0;
        -(val as i64)
    }

    /// For C interop.
    #[inline]
    pub const fn to_negated_i32(self) -> i32 {
        let val: u16 = self.0;
        -(val as i32)
    }

    /// Primarily for checked conversion of bindings constants.
    const fn from_u32_const(val: u32) -> Self {
        let Some(rv) = Self::from_u16(val as u16) else {
            panic!("Could not construct an `Errno`");
        };
        // check for truncation
        assert!(rv.0 as u32 == val);
        rv
    }

    /// Get libc's errno (global for the current thread).
    #[cfg(feature = "libc")]
    pub fn from_libc_errno() -> Self {
        // SAFETY: The safety requirements for calling this function are
        // undocumented, and I'm not aware of any.
        let ptr = unsafe { libc::__errno_location() };
        assert!(!ptr.is_null());
        // SAFETY: We verified that `ptr` is non-NULL, and can only assume that
        // libc gives us a pointer that is safe to dereference.
        //
        // `errno(2)` guarantees that it's thread-local, so there shouldn't be any
        // data-races from other threads.
        //
        // A signal handler could run on the current thread and change the `errno`
        // value at `*ptr`. A well-behaved handler should always restore the original
        // value before returning (`signal-safety(7)`) which should make this
        // sound; nonetheless we try to make the dereference "as atomic" as
        // possible, e.g.  by directly dereferencing the pointer and not
        // converting it to a Rust reference.
        let raw = unsafe { *ptr };

        Self::from_libc_errnum(raw)
            .unwrap_or_else(|| panic!("Unexpected bad errno from libc: {raw}"))
    }

    /// Get a Result from the return value of a libc function that uses a
    /// sentinel error value, and stores the errors themselves in errno.
    #[cfg(feature = "libc")]
    pub fn result_from_libc_errno<T>(sentinel: T, x: T) -> Result<T, Self>
    where
        T: Eq,
    {
        if x == sentinel {
            Err(Self::from_libc_errno())
        } else {
            Ok(x)
        }
    }

    /// Get a Result from a libc `errnum` return value, where 0 is used to
    /// indicate "no error", and non-zero is an errno value. An example of such
    /// a function is `libc::posix_spawn`.
    ///
    /// Panics if `errnum` is out of range. This shouldn't be the case for any `errnum` obtained
    /// from libc APIs.
    #[cfg(feature = "libc")]
    pub fn result_from_libc_errnum(errnum: i32) -> Result<(), Self> {
        if errnum == 0 {
            Ok(())
        } else {
            Err(Self::from_libc_errnum(errnum)
                .unwrap_or_else(|| panic!("errnum out of range: {errnum}")))
        }
    }

    /// Convert from a libc error value. Returns `None` if out of range.
    #[cfg(feature = "libc")]
    pub fn from_libc_errnum(errnum: i32) -> Option<Self> {
        // For now we assume that libc uses the same errnum values
        // as the kernel. This isn't explicitly guaranteed, but we don't
        // know of any exceptions in practice. In case it turns out not to be true
        // we can either change this to a full explicit mapping, or handle
        // individual known exceptions here.
        Self::from_u16(u16::try_from(errnum).ok()?)
    }
}

impl core::convert::From<linux_errno::Error> for Errno {
    fn from(value: linux_errno::Error) -> Self {
        // linux_errno::Error values should always be in range.
        Self::try_from(value.get()).unwrap()
    }
}

#[cfg(feature = "std")]
impl core::convert::From<Errno> for std::io::Error {
    fn from(e: Errno) -> Self {
        Self::from_raw_os_error(e.into())
    }
}

#[cfg(feature = "std")]
impl core::convert::TryFrom<std::io::Error> for Errno {
    type Error = std::io::Error;

    fn try_from(e: std::io::Error) -> Result<Self, Self::Error> {
        e.raw_os_error()
            .and_then(|x| u16::try_from(x).ok())
            .and_then(|x| x.try_into().ok())
            .ok_or(e)
    }
}

#[cfg(feature = "std")]
impl std::error::Error for Errno {}
