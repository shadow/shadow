use num_enum::{FromPrimitive, IntoPrimitive};

use crate::bindings;

// Clocks
#[derive(Debug, Copy, Clone, IntoPrimitive, FromPrimitive)]
#[repr(u16)]
#[allow(non_camel_case_types)]
pub enum Errno {
    EINVAL = bindings::EINVAL as u16,
    EDEADLK = bindings::EDEADLK as u16,
    ENAMETOOLONG = bindings::ENAMETOOLONG as u16,
    ENOLCK = bindings::ENOLCK as u16,
    ENOSYS = bindings::ENOSYS as u16,
    ENOTEMPTY = bindings::ENOTEMPTY as u16,
    ELOOP = bindings::ELOOP as u16,
    EWOULDBLOCK = bindings::EWOULDBLOCK as u16,
    ENOMSG = bindings::ENOMSG as u16,
    EIDRM = bindings::EIDRM as u16,
    ECHRNG = bindings::ECHRNG as u16,
    EL2NSYNC = bindings::EL2NSYNC as u16,
    EL3HLT = bindings::EL3HLT as u16,
    EL3RST = bindings::EL3RST as u16,
    ELNRNG = bindings::ELNRNG as u16,
    EUNATCH = bindings::EUNATCH as u16,
    ENOCSI = bindings::ENOCSI as u16,
    EL2HLT = bindings::EL2HLT as u16,
    EBADE = bindings::EBADE as u16,
    EBADR = bindings::EBADR as u16,
    EXFULL = bindings::EXFULL as u16,
    ENOANO = bindings::ENOANO as u16,
    EBADRQC = bindings::EBADRQC as u16,
    EBADSLT = bindings::EBADSLT as u16,
    EBFONT = bindings::EBFONT as u16,
    ENOSTR = bindings::ENOSTR as u16,
    ENODATA = bindings::ENODATA as u16,
    ETIME = bindings::ETIME as u16,
    ENOSR = bindings::ENOSR as u16,
    ENONET = bindings::ENONET as u16,
    ENOPKG = bindings::ENOPKG as u16,
    EREMOTE = bindings::EREMOTE as u16,
    ENOLINK = bindings::ENOLINK as u16,
    EADV = bindings::EADV as u16,
    ESRMNT = bindings::ESRMNT as u16,
    ECOMM = bindings::ECOMM as u16,
    EPROTO = bindings::EPROTO as u16,
    EMULTIHOP = bindings::EMULTIHOP as u16,
    EDOTDOT = bindings::EDOTDOT as u16,
    EBADMSG = bindings::EBADMSG as u16,
    EOVERFLOW = bindings::EOVERFLOW as u16,
    ENOTUNIQ = bindings::ENOTUNIQ as u16,
    EBADFD = bindings::EBADFD as u16,
    EREMCHG = bindings::EREMCHG as u16,
    ELIBACC = bindings::ELIBACC as u16,
    ELIBBAD = bindings::ELIBBAD as u16,
    ELIBSCN = bindings::ELIBSCN as u16,
    ELIBMAX = bindings::ELIBMAX as u16,
    ELIBEXEC = bindings::ELIBEXEC as u16,
    EILSEQ = bindings::EILSEQ as u16,
    ERESTART = bindings::ERESTART as u16,
    ESTRPIPE = bindings::ESTRPIPE as u16,
    EUSERS = bindings::EUSERS as u16,
    ENOTSOCK = bindings::ENOTSOCK as u16,
    EDESTADDRREQ = bindings::EDESTADDRREQ as u16,
    EMSGSIZE = bindings::EMSGSIZE as u16,
    EPROTOTYPE = bindings::EPROTOTYPE as u16,
    ENOPROTOOPT = bindings::ENOPROTOOPT as u16,
    EPROTONOSUPPORT = bindings::EPROTONOSUPPORT as u16,
    ESOCKTNOSUPPORT = bindings::ESOCKTNOSUPPORT as u16,
    EOPNOTSUPP = bindings::EOPNOTSUPP as u16,
    EPFNOSUPPORT = bindings::EPFNOSUPPORT as u16,
    EAFNOSUPPORT = bindings::EAFNOSUPPORT as u16,
    EADDRINUSE = bindings::EADDRINUSE as u16,
    EADDRNOTAVAIL = bindings::EADDRNOTAVAIL as u16,
    ENETDOWN = bindings::ENETDOWN as u16,
    ENETUNREACH = bindings::ENETUNREACH as u16,
    ENETRESET = bindings::ENETRESET as u16,
    ECONNABORTED = bindings::ECONNABORTED as u16,
    ECONNRESET = bindings::ECONNRESET as u16,
    ENOBUFS = bindings::ENOBUFS as u16,
    EISCONN = bindings::EISCONN as u16,
    ENOTCONN = bindings::ENOTCONN as u16,
    ESHUTDOWN = bindings::ESHUTDOWN as u16,
    ETOOMANYREFS = bindings::ETOOMANYREFS as u16,
    ETIMEDOUT = bindings::ETIMEDOUT as u16,
    ECONNREFUSED = bindings::ECONNREFUSED as u16,
    EHOSTDOWN = bindings::EHOSTDOWN as u16,
    EHOSTUNREACH = bindings::EHOSTUNREACH as u16,
    EALREADY = bindings::EALREADY as u16,
    EINPROGRESS = bindings::EINPROGRESS as u16,
    ESTALE = bindings::ESTALE as u16,
    EUCLEAN = bindings::EUCLEAN as u16,
    ENOTNAM = bindings::ENOTNAM as u16,
    ENAVAIL = bindings::ENAVAIL as u16,
    EISNAM = bindings::EISNAM as u16,
    EREMOTEIO = bindings::EREMOTEIO as u16,
    EDQUOT = bindings::EDQUOT as u16,
    ENOMEDIUM = bindings::ENOMEDIUM as u16,
    EMEDIUMTYPE = bindings::EMEDIUMTYPE as u16,
    ECANCELED = bindings::ECANCELED as u16,
    ENOKEY = bindings::ENOKEY as u16,
    EKEYEXPIRED = bindings::EKEYEXPIRED as u16,
    EKEYREVOKED = bindings::EKEYREVOKED as u16,
    EKEYREJECTED = bindings::EKEYREJECTED as u16,
    EOWNERDEAD = bindings::EOWNERDEAD as u16,
    ENOTRECOVERABLE = bindings::ENOTRECOVERABLE as u16,
    ERFKILL = bindings::ERFKILL as u16,
    EHWPOISON = bindings::EHWPOISON as u16,

    #[num_enum(catch_all)]
    EUNKNOWN(u16),
}

impl Errno {
    pub const EDEADLOCK: Self = Self::EDEADLK;

    pub fn to_negated_i64(self) -> i64 {
        -(u16::from(self) as i64)
    }

    pub fn to_negated_i32(self) -> i32 {
        -(u16::from(self) as i32)
    }
}

impl core::convert::From<linux_errno::Error> for Errno {
    fn from(value: linux_errno::Error) -> Self {
        Self::from(value.get())
    }
}
