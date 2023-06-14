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
        if (1..=(Self::MAX.0)).contains(&val) {
            Ok(Self(val))
        } else {
            Err(())
        }
    }
}

impl From<Errno> for u16 {
    fn from(val: Errno) -> u16 {
        val.0
    }
}

impl From<Errno> for u32 {
    fn from(val: Errno) -> u32 {
        val.0.into()
    }
}

impl From<Errno> for u64 {
    fn from(val: Errno) -> u64 {
        val.0.into()
    }
}

impl From<Errno> for i32 {
    fn from(val: Errno) -> i32 {
        val.0.into()
    }
}

impl From<Errno> for i64 {
    fn from(val: Errno) -> i64 {
        val.0.into()
    }
}

fn errno_to_str(e: Errno) -> Option<&'static str> {
    match u32::from(e.0) {
        bindings::LINUX_EINVAL => Some("EINVAL"),
        bindings::LINUX_EDEADLK => Some("EDEADLK"),
        bindings::LINUX_ENAMETOOLONG => Some("ENAMETOOLONG"),
        bindings::LINUX_ENOLCK => Some("ENOLCK"),
        bindings::LINUX_ENOSYS => Some("ENOSYS"),
        bindings::LINUX_ENOTEMPTY => Some("ENOTEMPTY"),
        bindings::LINUX_ELOOP => Some("ELOOP"),
        bindings::LINUX_EWOULDBLOCK => Some("EWOULDBLOCK"),
        bindings::LINUX_ENOMSG => Some("ENOMSG"),
        bindings::LINUX_EIDRM => Some("EIDRM"),
        bindings::LINUX_ECHRNG => Some("ECHRNG"),
        bindings::LINUX_EL2NSYNC => Some("EL2NSYNC"),
        bindings::LINUX_EL3HLT => Some("EL3HLT"),
        bindings::LINUX_EL3RST => Some("EL3RST"),
        bindings::LINUX_ELNRNG => Some("ELNRNG"),
        bindings::LINUX_EUNATCH => Some("EUNATCH"),
        bindings::LINUX_ENOCSI => Some("ENOCSI"),
        bindings::LINUX_EL2HLT => Some("EL2HLT"),
        bindings::LINUX_EBADE => Some("EBADE"),
        bindings::LINUX_EBADR => Some("EBADR"),
        bindings::LINUX_EXFULL => Some("EXFULL"),
        bindings::LINUX_ENOANO => Some("ENOANO"),
        bindings::LINUX_EBADRQC => Some("EBADRQC"),
        bindings::LINUX_EBADSLT => Some("EBADSLT"),
        bindings::LINUX_EBFONT => Some("EBFONT"),
        bindings::LINUX_ENOSTR => Some("ENOSTR"),
        bindings::LINUX_ENODATA => Some("ENODATA"),
        bindings::LINUX_ETIME => Some("ETIME"),
        bindings::LINUX_ENOSR => Some("ENOSR"),
        bindings::LINUX_ENONET => Some("ENONET"),
        bindings::LINUX_ENOPKG => Some("ENOPKG"),
        bindings::LINUX_EREMOTE => Some("EREMOTE"),
        bindings::LINUX_ENOLINK => Some("ENOLINK"),
        bindings::LINUX_EADV => Some("EADV"),
        bindings::LINUX_ESRMNT => Some("ESRMNT"),
        bindings::LINUX_ECOMM => Some("ECOMM"),
        bindings::LINUX_EPROTO => Some("EPROTO"),
        bindings::LINUX_EMULTIHOP => Some("EMULTIHOP"),
        bindings::LINUX_EDOTDOT => Some("EDOTDOT"),
        bindings::LINUX_EBADMSG => Some("EBADMSG"),
        bindings::LINUX_EOVERFLOW => Some("EOVERFLOW"),
        bindings::LINUX_ENOTUNIQ => Some("ENOTUNIQ"),
        bindings::LINUX_EBADFD => Some("EBADFD"),
        bindings::LINUX_EREMCHG => Some("EREMCHG"),
        bindings::LINUX_ELIBACC => Some("ELIBACC"),
        bindings::LINUX_ELIBBAD => Some("ELIBBAD"),
        bindings::LINUX_ELIBSCN => Some("ELIBSCN"),
        bindings::LINUX_ELIBMAX => Some("ELIBMAX"),
        bindings::LINUX_ELIBEXEC => Some("ELIBEXEC"),
        bindings::LINUX_EILSEQ => Some("EILSEQ"),
        bindings::LINUX_ERESTART => Some("ERESTART"),
        bindings::LINUX_ESTRPIPE => Some("ESTRPIPE"),
        bindings::LINUX_EUSERS => Some("EUSERS"),
        bindings::LINUX_ENOTSOCK => Some("ENOTSOCK"),
        bindings::LINUX_EDESTADDRREQ => Some("EDESTADDRREQ"),
        bindings::LINUX_EMSGSIZE => Some("EMSGSIZE"),
        bindings::LINUX_EPROTOTYPE => Some("EPROTOTYPE"),
        bindings::LINUX_ENOPROTOOPT => Some("ENOPROTOOPT"),
        bindings::LINUX_EPROTONOSUPPORT => Some("EPROTONOSUPPORT"),
        bindings::LINUX_ESOCKTNOSUPPORT => Some("ESOCKTNOSUPPORT"),
        bindings::LINUX_EOPNOTSUPP => Some("EOPNOTSUPP"),
        bindings::LINUX_EPFNOSUPPORT => Some("EPFNOSUPPORT"),
        bindings::LINUX_EAFNOSUPPORT => Some("EAFNOSUPPORT"),
        bindings::LINUX_EADDRINUSE => Some("EADDRINUSE"),
        bindings::LINUX_EADDRNOTAVAIL => Some("EADDRNOTAVAIL"),
        bindings::LINUX_ENETDOWN => Some("ENETDOWN"),
        bindings::LINUX_ENETUNREACH => Some("ENETUNREACH"),
        bindings::LINUX_ENETRESET => Some("ENETRESET"),
        bindings::LINUX_ECONNABORTED => Some("ECONNABORTED"),
        bindings::LINUX_ECONNRESET => Some("ECONNRESET"),
        bindings::LINUX_ENOBUFS => Some("ENOBUFS"),
        bindings::LINUX_EISCONN => Some("EISCONN"),
        bindings::LINUX_ENOTCONN => Some("ENOTCONN"),
        bindings::LINUX_ESHUTDOWN => Some("ESHUTDOWN"),
        bindings::LINUX_ETOOMANYREFS => Some("ETOOMANYREFS"),
        bindings::LINUX_ETIMEDOUT => Some("ETIMEDOUT"),
        bindings::LINUX_ECONNREFUSED => Some("ECONNREFUSED"),
        bindings::LINUX_EHOSTDOWN => Some("EHOSTDOWN"),
        bindings::LINUX_EHOSTUNREACH => Some("EHOSTUNREACH"),
        bindings::LINUX_EALREADY => Some("EALREADY"),
        bindings::LINUX_EINPROGRESS => Some("EINPROGRESS"),
        bindings::LINUX_ESTALE => Some("ESTALE"),
        bindings::LINUX_EUCLEAN => Some("EUCLEAN"),
        bindings::LINUX_ENOTNAM => Some("ENOTNAM"),
        bindings::LINUX_ENAVAIL => Some("ENAVAIL"),
        bindings::LINUX_EISNAM => Some("EISNAM"),
        bindings::LINUX_EREMOTEIO => Some("EREMOTEIO"),
        bindings::LINUX_EDQUOT => Some("EDQUOT"),
        bindings::LINUX_ENOMEDIUM => Some("ENOMEDIUM"),
        bindings::LINUX_EMEDIUMTYPE => Some("EMEDIUMTYPE"),
        bindings::LINUX_ECANCELED => Some("ECANCELED"),
        bindings::LINUX_ENOKEY => Some("ENOKEY"),
        bindings::LINUX_EKEYEXPIRED => Some("EKEYEXPIRED"),
        bindings::LINUX_EKEYREVOKED => Some("EKEYREVOKED"),
        bindings::LINUX_EKEYREJECTED => Some("EKEYREJECTED"),
        bindings::LINUX_EOWNERDEAD => Some("EOWNERDEAD"),
        bindings::LINUX_ENOTRECOVERABLE => Some("ENOTRECOVERABLE"),
        bindings::LINUX_ERFKILL => Some("ERFKILL"),
        bindings::LINUX_EHWPOISON => Some("EHWPOISON"),
        bindings::LINUX_EINTR => Some("EINTR"),
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

    // Aliases
    pub const EDEADLOCK: Self = Self::from_u32_const(bindings::LINUX_EDEADLOCK);
    pub const EAGAIN: Self = Self::from_u32_const(bindings::LINUX_EAGAIN);
    pub const ENOTSUP: Self = Self::EOPNOTSUPP;

    /// From MAX_ERRNO in include/linux/err.h in kernel source. This doesn't
    /// seem to be exposed in the installed kernel headers from which we generate bindings.
    /// <https://github.com/torvalds/linux/blob/a4d7d701121981e3c3fe69ade376fe9f26324161/include/linux/err.h#L18>
    pub const MAX: Self = Self(4095);

    /// For C interop.
    pub fn to_negated_i64(self) -> i64 {
        -(u16::from(self) as i64)
    }

    /// For C interop.
    pub fn to_negated_i32(self) -> i32 {
        -(u16::from(self) as i32)
    }

    // Primarily for checked conversion of bindings constants.
    const fn from_u32_const(val: u32) -> Self {
        let rv = Self(val as u16);
        // check for truncation
        assert!(rv.0 as u32 == val);
        // Don't allow out-of-range values
        assert!(rv.0 <= Self::MAX.0);
        rv
    }
}

impl core::convert::From<linux_errno::Error> for Errno {
    fn from(value: linux_errno::Error) -> Self {
        // linux_errno::Error values should always be in range.
        Self::try_from(value.get()).unwrap()
    }
}
