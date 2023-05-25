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
    pub const EINVAL: Self = Self(bindings::LINUX_EINVAL as u16);
    pub const EDEADLK: Self = Self(bindings::LINUX_EDEADLK as u16);
    pub const ENAMETOOLONG: Self = Self(bindings::LINUX_ENAMETOOLONG as u16);
    pub const ENOLCK: Self = Self(bindings::LINUX_ENOLCK as u16);
    pub const ENOSYS: Self = Self(bindings::LINUX_ENOSYS as u16);
    pub const ENOTEMPTY: Self = Self(bindings::LINUX_ENOTEMPTY as u16);
    pub const ELOOP: Self = Self(bindings::LINUX_ELOOP as u16);
    pub const EWOULDBLOCK: Self = Self(bindings::LINUX_EWOULDBLOCK as u16);
    pub const ENOMSG: Self = Self(bindings::LINUX_ENOMSG as u16);
    pub const EIDRM: Self = Self(bindings::LINUX_EIDRM as u16);
    pub const ECHRNG: Self = Self(bindings::LINUX_ECHRNG as u16);
    pub const EL2NSYNC: Self = Self(bindings::LINUX_EL2NSYNC as u16);
    pub const EL3HLT: Self = Self(bindings::LINUX_EL3HLT as u16);
    pub const EL3RST: Self = Self(bindings::LINUX_EL3RST as u16);
    pub const ELNRNG: Self = Self(bindings::LINUX_ELNRNG as u16);
    pub const EUNATCH: Self = Self(bindings::LINUX_EUNATCH as u16);
    pub const ENOCSI: Self = Self(bindings::LINUX_ENOCSI as u16);
    pub const EL2HLT: Self = Self(bindings::LINUX_EL2HLT as u16);
    pub const EBADE: Self = Self(bindings::LINUX_EBADE as u16);
    pub const EBADR: Self = Self(bindings::LINUX_EBADR as u16);
    pub const EXFULL: Self = Self(bindings::LINUX_EXFULL as u16);
    pub const ENOANO: Self = Self(bindings::LINUX_ENOANO as u16);
    pub const EBADRQC: Self = Self(bindings::LINUX_EBADRQC as u16);
    pub const EBADSLT: Self = Self(bindings::LINUX_EBADSLT as u16);
    pub const EBFONT: Self = Self(bindings::LINUX_EBFONT as u16);
    pub const ENOSTR: Self = Self(bindings::LINUX_ENOSTR as u16);
    pub const ENODATA: Self = Self(bindings::LINUX_ENODATA as u16);
    pub const ETIME: Self = Self(bindings::LINUX_ETIME as u16);
    pub const ENOSR: Self = Self(bindings::LINUX_ENOSR as u16);
    pub const ENONET: Self = Self(bindings::LINUX_ENONET as u16);
    pub const ENOPKG: Self = Self(bindings::LINUX_ENOPKG as u16);
    pub const EREMOTE: Self = Self(bindings::LINUX_EREMOTE as u16);
    pub const ENOLINK: Self = Self(bindings::LINUX_ENOLINK as u16);
    pub const EADV: Self = Self(bindings::LINUX_EADV as u16);
    pub const ESRMNT: Self = Self(bindings::LINUX_ESRMNT as u16);
    pub const ECOMM: Self = Self(bindings::LINUX_ECOMM as u16);
    pub const EPROTO: Self = Self(bindings::LINUX_EPROTO as u16);
    pub const EMULTIHOP: Self = Self(bindings::LINUX_EMULTIHOP as u16);
    pub const EDOTDOT: Self = Self(bindings::LINUX_EDOTDOT as u16);
    pub const EBADMSG: Self = Self(bindings::LINUX_EBADMSG as u16);
    pub const EOVERFLOW: Self = Self(bindings::LINUX_EOVERFLOW as u16);
    pub const ENOTUNIQ: Self = Self(bindings::LINUX_ENOTUNIQ as u16);
    pub const EBADFD: Self = Self(bindings::LINUX_EBADFD as u16);
    pub const EREMCHG: Self = Self(bindings::LINUX_EREMCHG as u16);
    pub const ELIBACC: Self = Self(bindings::LINUX_ELIBACC as u16);
    pub const ELIBBAD: Self = Self(bindings::LINUX_ELIBBAD as u16);
    pub const ELIBSCN: Self = Self(bindings::LINUX_ELIBSCN as u16);
    pub const ELIBMAX: Self = Self(bindings::LINUX_ELIBMAX as u16);
    pub const ELIBEXEC: Self = Self(bindings::LINUX_ELIBEXEC as u16);
    pub const EILSEQ: Self = Self(bindings::LINUX_EILSEQ as u16);
    pub const ERESTART: Self = Self(bindings::LINUX_ERESTART as u16);
    pub const ESTRPIPE: Self = Self(bindings::LINUX_ESTRPIPE as u16);
    pub const EUSERS: Self = Self(bindings::LINUX_EUSERS as u16);
    pub const ENOTSOCK: Self = Self(bindings::LINUX_ENOTSOCK as u16);
    pub const EDESTADDRREQ: Self = Self(bindings::LINUX_EDESTADDRREQ as u16);
    pub const EMSGSIZE: Self = Self(bindings::LINUX_EMSGSIZE as u16);
    pub const EPROTOTYPE: Self = Self(bindings::LINUX_EPROTOTYPE as u16);
    pub const ENOPROTOOPT: Self = Self(bindings::LINUX_ENOPROTOOPT as u16);
    pub const EPROTONOSUPPORT: Self = Self(bindings::LINUX_EPROTONOSUPPORT as u16);
    pub const ESOCKTNOSUPPORT: Self = Self(bindings::LINUX_ESOCKTNOSUPPORT as u16);
    pub const EOPNOTSUPP: Self = Self(bindings::LINUX_EOPNOTSUPP as u16);
    pub const EPFNOSUPPORT: Self = Self(bindings::LINUX_EPFNOSUPPORT as u16);
    pub const EAFNOSUPPORT: Self = Self(bindings::LINUX_EAFNOSUPPORT as u16);
    pub const EADDRINUSE: Self = Self(bindings::LINUX_EADDRINUSE as u16);
    pub const EADDRNOTAVAIL: Self = Self(bindings::LINUX_EADDRNOTAVAIL as u16);
    pub const ENETDOWN: Self = Self(bindings::LINUX_ENETDOWN as u16);
    pub const ENETUNREACH: Self = Self(bindings::LINUX_ENETUNREACH as u16);
    pub const ENETRESET: Self = Self(bindings::LINUX_ENETRESET as u16);
    pub const ECONNABORTED: Self = Self(bindings::LINUX_ECONNABORTED as u16);
    pub const ECONNRESET: Self = Self(bindings::LINUX_ECONNRESET as u16);
    pub const ENOBUFS: Self = Self(bindings::LINUX_ENOBUFS as u16);
    pub const EISCONN: Self = Self(bindings::LINUX_EISCONN as u16);
    pub const ENOTCONN: Self = Self(bindings::LINUX_ENOTCONN as u16);
    pub const ESHUTDOWN: Self = Self(bindings::LINUX_ESHUTDOWN as u16);
    pub const ETOOMANYREFS: Self = Self(bindings::LINUX_ETOOMANYREFS as u16);
    pub const ETIMEDOUT: Self = Self(bindings::LINUX_ETIMEDOUT as u16);
    pub const ECONNREFUSED: Self = Self(bindings::LINUX_ECONNREFUSED as u16);
    pub const EHOSTDOWN: Self = Self(bindings::LINUX_EHOSTDOWN as u16);
    pub const EHOSTUNREACH: Self = Self(bindings::LINUX_EHOSTUNREACH as u16);
    pub const EALREADY: Self = Self(bindings::LINUX_EALREADY as u16);
    pub const EINPROGRESS: Self = Self(bindings::LINUX_EINPROGRESS as u16);
    pub const ESTALE: Self = Self(bindings::LINUX_ESTALE as u16);
    pub const EUCLEAN: Self = Self(bindings::LINUX_EUCLEAN as u16);
    pub const ENOTNAM: Self = Self(bindings::LINUX_ENOTNAM as u16);
    pub const ENAVAIL: Self = Self(bindings::LINUX_ENAVAIL as u16);
    pub const EISNAM: Self = Self(bindings::LINUX_EISNAM as u16);
    pub const EREMOTEIO: Self = Self(bindings::LINUX_EREMOTEIO as u16);
    pub const EDQUOT: Self = Self(bindings::LINUX_EDQUOT as u16);
    pub const ENOMEDIUM: Self = Self(bindings::LINUX_ENOMEDIUM as u16);
    pub const EMEDIUMTYPE: Self = Self(bindings::LINUX_EMEDIUMTYPE as u16);
    pub const ECANCELED: Self = Self(bindings::LINUX_ECANCELED as u16);
    pub const ENOKEY: Self = Self(bindings::LINUX_ENOKEY as u16);
    pub const EKEYEXPIRED: Self = Self(bindings::LINUX_EKEYEXPIRED as u16);
    pub const EKEYREVOKED: Self = Self(bindings::LINUX_EKEYREVOKED as u16);
    pub const EKEYREJECTED: Self = Self(bindings::LINUX_EKEYREJECTED as u16);
    pub const EOWNERDEAD: Self = Self(bindings::LINUX_EOWNERDEAD as u16);
    pub const ENOTRECOVERABLE: Self = Self(bindings::LINUX_ENOTRECOVERABLE as u16);
    pub const ERFKILL: Self = Self(bindings::LINUX_ERFKILL as u16);
    pub const EHWPOISON: Self = Self(bindings::LINUX_EHWPOISON as u16);

    // Aliases
    pub const EDEADLOCK: Self = Self(bindings::LINUX_EDEADLOCK as u16);
    pub const EAGAIN: Self = Self(bindings::LINUX_EAGAIN as u16);

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
}

impl core::convert::From<linux_errno::Error> for Errno {
    fn from(value: linux_errno::Error) -> Self {
        // linux_errno::Error values should always be in range.
        Self::try_from(value.get()).unwrap()
    }
}
