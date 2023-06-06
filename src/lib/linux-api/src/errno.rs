use crate::bindings;

#[derive(Copy, Clone)]
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

impl core::fmt::Debug for Errno {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match u32::from(self.0) {
            bindings::LINUX_EINVAL => formatter.write_str("Errno::EINVAL"),
            bindings::LINUX_EDEADLK => formatter.write_str("Errno::EDEADLK"),
            bindings::LINUX_ENAMETOOLONG => formatter.write_str("Errno::ENAMETOOLONG"),
            bindings::LINUX_ENOLCK => formatter.write_str("Errno::ENOLCK"),
            bindings::LINUX_ENOSYS => formatter.write_str("Errno::ENOSYS"),
            bindings::LINUX_ENOTEMPTY => formatter.write_str("Errno::ENOTEMPTY"),
            bindings::LINUX_ELOOP => formatter.write_str("Errno::ELOOP"),
            bindings::LINUX_EWOULDBLOCK => formatter.write_str("Errno::EWOULDBLOCK"),
            bindings::LINUX_ENOMSG => formatter.write_str("Errno::ENOMSG"),
            bindings::LINUX_EIDRM => formatter.write_str("Errno::EIDRM"),
            bindings::LINUX_ECHRNG => formatter.write_str("Errno::ECHRNG"),
            bindings::LINUX_EL2NSYNC => formatter.write_str("Errno::EL2NSYNC"),
            bindings::LINUX_EL3HLT => formatter.write_str("Errno::EL3HLT"),
            bindings::LINUX_EL3RST => formatter.write_str("Errno::EL3RST"),
            bindings::LINUX_ELNRNG => formatter.write_str("Errno::ELNRNG"),
            bindings::LINUX_EUNATCH => formatter.write_str("Errno::EUNATCH"),
            bindings::LINUX_ENOCSI => formatter.write_str("Errno::ENOCSI"),
            bindings::LINUX_EL2HLT => formatter.write_str("Errno::EL2HLT"),
            bindings::LINUX_EBADE => formatter.write_str("Errno::EBADE"),
            bindings::LINUX_EBADR => formatter.write_str("Errno::EBADR"),
            bindings::LINUX_EXFULL => formatter.write_str("Errno::EXFULL"),
            bindings::LINUX_ENOANO => formatter.write_str("Errno::ENOANO"),
            bindings::LINUX_EBADRQC => formatter.write_str("Errno::EBADRQC"),
            bindings::LINUX_EBADSLT => formatter.write_str("Errno::EBADSLT"),
            bindings::LINUX_EBFONT => formatter.write_str("Errno::EBFONT"),
            bindings::LINUX_ENOSTR => formatter.write_str("Errno::ENOSTR"),
            bindings::LINUX_ENODATA => formatter.write_str("Errno::ENODATA"),
            bindings::LINUX_ETIME => formatter.write_str("Errno::ETIME"),
            bindings::LINUX_ENOSR => formatter.write_str("Errno::ENOSR"),
            bindings::LINUX_ENONET => formatter.write_str("Errno::ENONET"),
            bindings::LINUX_ENOPKG => formatter.write_str("Errno::ENOPKG"),
            bindings::LINUX_EREMOTE => formatter.write_str("Errno::EREMOTE"),
            bindings::LINUX_ENOLINK => formatter.write_str("Errno::ENOLINK"),
            bindings::LINUX_EADV => formatter.write_str("Errno::EADV"),
            bindings::LINUX_ESRMNT => formatter.write_str("Errno::ESRMNT"),
            bindings::LINUX_ECOMM => formatter.write_str("Errno::ECOMM"),
            bindings::LINUX_EPROTO => formatter.write_str("Errno::EPROTO"),
            bindings::LINUX_EMULTIHOP => formatter.write_str("Errno::EMULTIHOP"),
            bindings::LINUX_EDOTDOT => formatter.write_str("Errno::EDOTDOT"),
            bindings::LINUX_EBADMSG => formatter.write_str("Errno::EBADMSG"),
            bindings::LINUX_EOVERFLOW => formatter.write_str("Errno::EOVERFLOW"),
            bindings::LINUX_ENOTUNIQ => formatter.write_str("Errno::ENOTUNIQ"),
            bindings::LINUX_EBADFD => formatter.write_str("Errno::EBADFD"),
            bindings::LINUX_EREMCHG => formatter.write_str("Errno::EREMCHG"),
            bindings::LINUX_ELIBACC => formatter.write_str("Errno::ELIBACC"),
            bindings::LINUX_ELIBBAD => formatter.write_str("Errno::ELIBBAD"),
            bindings::LINUX_ELIBSCN => formatter.write_str("Errno::ELIBSCN"),
            bindings::LINUX_ELIBMAX => formatter.write_str("Errno::ELIBMAX"),
            bindings::LINUX_ELIBEXEC => formatter.write_str("Errno::ELIBEXEC"),
            bindings::LINUX_EILSEQ => formatter.write_str("Errno::EILSEQ"),
            bindings::LINUX_ERESTART => formatter.write_str("Errno::ERESTART"),
            bindings::LINUX_ESTRPIPE => formatter.write_str("Errno::ESTRPIPE"),
            bindings::LINUX_EUSERS => formatter.write_str("Errno::EUSERS"),
            bindings::LINUX_ENOTSOCK => formatter.write_str("Errno::ENOTSOCK"),
            bindings::LINUX_EDESTADDRREQ => formatter.write_str("Errno::EDESTADDRREQ"),
            bindings::LINUX_EMSGSIZE => formatter.write_str("Errno::EMSGSIZE"),
            bindings::LINUX_EPROTOTYPE => formatter.write_str("Errno::EPROTOTYPE"),
            bindings::LINUX_ENOPROTOOPT => formatter.write_str("Errno::ENOPROTOOPT"),
            bindings::LINUX_EPROTONOSUPPORT => formatter.write_str("Errno::EPROTONOSUPPORT"),
            bindings::LINUX_ESOCKTNOSUPPORT => formatter.write_str("Errno::ESOCKTNOSUPPORT"),
            bindings::LINUX_EOPNOTSUPP => formatter.write_str("Errno::EOPNOTSUPP"),
            bindings::LINUX_EPFNOSUPPORT => formatter.write_str("Errno::EPFNOSUPPORT"),
            bindings::LINUX_EAFNOSUPPORT => formatter.write_str("Errno::EAFNOSUPPORT"),
            bindings::LINUX_EADDRINUSE => formatter.write_str("Errno::EADDRINUSE"),
            bindings::LINUX_EADDRNOTAVAIL => formatter.write_str("Errno::EADDRNOTAVAIL"),
            bindings::LINUX_ENETDOWN => formatter.write_str("Errno::ENETDOWN"),
            bindings::LINUX_ENETUNREACH => formatter.write_str("Errno::ENETUNREACH"),
            bindings::LINUX_ENETRESET => formatter.write_str("Errno::ENETRESET"),
            bindings::LINUX_ECONNABORTED => formatter.write_str("Errno::ECONNABORTED"),
            bindings::LINUX_ECONNRESET => formatter.write_str("Errno::ECONNRESET"),
            bindings::LINUX_ENOBUFS => formatter.write_str("Errno::ENOBUFS"),
            bindings::LINUX_EISCONN => formatter.write_str("Errno::EISCONN"),
            bindings::LINUX_ENOTCONN => formatter.write_str("Errno::ENOTCONN"),
            bindings::LINUX_ESHUTDOWN => formatter.write_str("Errno::ESHUTDOWN"),
            bindings::LINUX_ETOOMANYREFS => formatter.write_str("Errno::ETOOMANYREFS"),
            bindings::LINUX_ETIMEDOUT => formatter.write_str("Errno::ETIMEDOUT"),
            bindings::LINUX_ECONNREFUSED => formatter.write_str("Errno::ECONNREFUSED"),
            bindings::LINUX_EHOSTDOWN => formatter.write_str("Errno::EHOSTDOWN"),
            bindings::LINUX_EHOSTUNREACH => formatter.write_str("Errno::EHOSTUNREACH"),
            bindings::LINUX_EALREADY => formatter.write_str("Errno::EALREADY"),
            bindings::LINUX_EINPROGRESS => formatter.write_str("Errno::EINPROGRESS"),
            bindings::LINUX_ESTALE => formatter.write_str("Errno::ESTALE"),
            bindings::LINUX_EUCLEAN => formatter.write_str("Errno::EUCLEAN"),
            bindings::LINUX_ENOTNAM => formatter.write_str("Errno::ENOTNAM"),
            bindings::LINUX_ENAVAIL => formatter.write_str("Errno::ENAVAIL"),
            bindings::LINUX_EISNAM => formatter.write_str("Errno::EISNAM"),
            bindings::LINUX_EREMOTEIO => formatter.write_str("Errno::EREMOTEIO"),
            bindings::LINUX_EDQUOT => formatter.write_str("Errno::EDQUOT"),
            bindings::LINUX_ENOMEDIUM => formatter.write_str("Errno::ENOMEDIUM"),
            bindings::LINUX_EMEDIUMTYPE => formatter.write_str("Errno::EMEDIUMTYPE"),
            bindings::LINUX_ECANCELED => formatter.write_str("Errno::ECANCELED"),
            bindings::LINUX_ENOKEY => formatter.write_str("Errno::ENOKEY"),
            bindings::LINUX_EKEYEXPIRED => formatter.write_str("Errno::EKEYEXPIRED"),
            bindings::LINUX_EKEYREVOKED => formatter.write_str("Errno::EKEYREVOKED"),
            bindings::LINUX_EKEYREJECTED => formatter.write_str("Errno::EKEYREJECTED"),
            bindings::LINUX_EOWNERDEAD => formatter.write_str("Errno::EOWNERDEAD"),
            bindings::LINUX_ENOTRECOVERABLE => formatter.write_str("Errno::ENOTRECOVERABLE"),
            bindings::LINUX_ERFKILL => formatter.write_str("Errno::ERFKILL"),
            bindings::LINUX_EHWPOISON => formatter.write_str("Errno::EHWPOISON"),
            x => formatter.write_fmt(format_args!("Errno::<{x}>")),
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

    // Aliases
    pub const EDEADLOCK: Self = Self::from_u32_const(bindings::LINUX_EDEADLOCK);
    pub const EAGAIN: Self = Self::from_u32_const(bindings::LINUX_EAGAIN);

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
