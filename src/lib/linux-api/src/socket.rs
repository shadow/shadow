use crate::{bindings, const_conversions};

#[allow(non_camel_case_types)]
pub type sa_family_t = bindings::linux___kernel_sa_family_t;

#[derive(Copy, Clone, Eq, PartialEq)]
pub struct AddressFamily(sa_family_t);

#[allow(non_upper_case_globals)]
impl AddressFamily {
    pub const AF_UNSPEC: Self = Self::from_u32(bindings::LINUX_AF_UNSPEC);
    pub const AF_UNIX: Self = Self::from_u32(bindings::LINUX_AF_UNIX);
    pub const AF_LOCAL: Self = Self::from_u32(bindings::LINUX_AF_LOCAL);
    pub const AF_INET: Self = Self::from_u32(bindings::LINUX_AF_INET);
    pub const AF_AX25: Self = Self::from_u32(bindings::LINUX_AF_AX25);
    pub const AF_IPX: Self = Self::from_u32(bindings::LINUX_AF_IPX);
    pub const AF_APPLETALK: Self = Self::from_u32(bindings::LINUX_AF_APPLETALK);
    pub const AF_NETROM: Self = Self::from_u32(bindings::LINUX_AF_NETROM);
    pub const AF_BRIDGE: Self = Self::from_u32(bindings::LINUX_AF_BRIDGE);
    pub const AF_ATMPVC: Self = Self::from_u32(bindings::LINUX_AF_ATMPVC);
    pub const AF_X25: Self = Self::from_u32(bindings::LINUX_AF_X25);
    pub const AF_INET6: Self = Self::from_u32(bindings::LINUX_AF_INET6);
    pub const AF_ROSE: Self = Self::from_u32(bindings::LINUX_AF_ROSE);
    pub const AF_DECnet: Self = Self::from_u32(bindings::LINUX_AF_DECnet);
    pub const AF_NETBEUI: Self = Self::from_u32(bindings::LINUX_AF_NETBEUI);
    pub const AF_SECURITY: Self = Self::from_u32(bindings::LINUX_AF_SECURITY);
    pub const AF_KEY: Self = Self::from_u32(bindings::LINUX_AF_KEY);
    pub const AF_NETLINK: Self = Self::from_u32(bindings::LINUX_AF_NETLINK);
    pub const AF_ROUTE: Self = Self::from_u32(bindings::LINUX_AF_ROUTE);
    pub const AF_PACKET: Self = Self::from_u32(bindings::LINUX_AF_PACKET);
    pub const AF_ASH: Self = Self::from_u32(bindings::LINUX_AF_ASH);
    pub const AF_ECONET: Self = Self::from_u32(bindings::LINUX_AF_ECONET);
    pub const AF_ATMSVC: Self = Self::from_u32(bindings::LINUX_AF_ATMSVC);
    pub const AF_RDS: Self = Self::from_u32(bindings::LINUX_AF_RDS);
    pub const AF_SNA: Self = Self::from_u32(bindings::LINUX_AF_SNA);
    pub const AF_IRDA: Self = Self::from_u32(bindings::LINUX_AF_IRDA);
    pub const AF_PPPOX: Self = Self::from_u32(bindings::LINUX_AF_PPPOX);
    pub const AF_WANPIPE: Self = Self::from_u32(bindings::LINUX_AF_WANPIPE);
    pub const AF_LLC: Self = Self::from_u32(bindings::LINUX_AF_LLC);
    pub const AF_IB: Self = Self::from_u32(bindings::LINUX_AF_IB);
    pub const AF_MPLS: Self = Self::from_u32(bindings::LINUX_AF_MPLS);
    pub const AF_CAN: Self = Self::from_u32(bindings::LINUX_AF_CAN);
    pub const AF_TIPC: Self = Self::from_u32(bindings::LINUX_AF_TIPC);
    pub const AF_BLUETOOTH: Self = Self::from_u32(bindings::LINUX_AF_BLUETOOTH);
    pub const AF_IUCV: Self = Self::from_u32(bindings::LINUX_AF_IUCV);
    pub const AF_RXRPC: Self = Self::from_u32(bindings::LINUX_AF_RXRPC);
    pub const AF_ISDN: Self = Self::from_u32(bindings::LINUX_AF_ISDN);
    pub const AF_PHONET: Self = Self::from_u32(bindings::LINUX_AF_PHONET);
    pub const AF_IEEE802154: Self = Self::from_u32(bindings::LINUX_AF_IEEE802154);
    pub const AF_CAIF: Self = Self::from_u32(bindings::LINUX_AF_CAIF);
    pub const AF_ALG: Self = Self::from_u32(bindings::LINUX_AF_ALG);
    pub const AF_NFC: Self = Self::from_u32(bindings::LINUX_AF_NFC);
    pub const AF_VSOCK: Self = Self::from_u32(bindings::LINUX_AF_VSOCK);
    pub const AF_KCM: Self = Self::from_u32(bindings::LINUX_AF_KCM);
    pub const AF_QIPCRTR: Self = Self::from_u32(bindings::LINUX_AF_QIPCRTR);
    pub const AF_SMC: Self = Self::from_u32(bindings::LINUX_AF_SMC);
    pub const AF_XDP: Self = Self::from_u32(bindings::LINUX_AF_XDP);
    pub const AF_MCTP: Self = Self::from_u32(bindings::LINUX_AF_MCTP);
    // add new entries to `to_str` below

    #[inline]
    pub const fn new(val: sa_family_t) -> Self {
        Self(val)
    }

    #[inline]
    pub const fn val(&self) -> sa_family_t {
        self.0
    }

    const fn from_u32(val: u32) -> Self {
        Self::new(const_conversions::u16_from_u32(val))
    }

    pub const fn to_str(&self) -> Option<&'static str> {
        match *self {
            Self::AF_UNSPEC => Some("AF_UNSPEC"),
            Self::AF_UNIX => Some("AF_UNIX"),
            // Self::AF_LOCAL == Self::AF_UNIX
            Self::AF_INET => Some("AF_INET"),
            Self::AF_AX25 => Some("AF_AX25"),
            Self::AF_IPX => Some("AF_IPX"),
            Self::AF_APPLETALK => Some("AF_APPLETALK"),
            Self::AF_NETROM => Some("AF_NETROM"),
            Self::AF_BRIDGE => Some("AF_BRIDGE"),
            Self::AF_ATMPVC => Some("AF_ATMPVC"),
            Self::AF_X25 => Some("AF_X25"),
            Self::AF_INET6 => Some("AF_INET6"),
            Self::AF_ROSE => Some("AF_ROSE"),
            Self::AF_DECnet => Some("AF_DECnet"),
            Self::AF_NETBEUI => Some("AF_NETBEUI"),
            Self::AF_SECURITY => Some("AF_SECURITY"),
            Self::AF_KEY => Some("AF_KEY"),
            Self::AF_NETLINK => Some("AF_NETLINK"),
            // Self::AF_ROUTE == Self::AF_NETLINK
            Self::AF_PACKET => Some("AF_PACKET"),
            Self::AF_ASH => Some("AF_ASH"),
            Self::AF_ECONET => Some("AF_ECONET"),
            Self::AF_ATMSVC => Some("AF_ATMSVC"),
            Self::AF_RDS => Some("AF_RDS"),
            Self::AF_SNA => Some("AF_SNA"),
            Self::AF_IRDA => Some("AF_IRDA"),
            Self::AF_PPPOX => Some("AF_PPPOX"),
            Self::AF_WANPIPE => Some("AF_WANPIPE"),
            Self::AF_LLC => Some("AF_LLC"),
            Self::AF_IB => Some("AF_IB"),
            Self::AF_MPLS => Some("AF_MPLS"),
            Self::AF_CAN => Some("AF_CAN"),
            Self::AF_TIPC => Some("AF_TIPC"),
            Self::AF_BLUETOOTH => Some("AF_BLUETOOTH"),
            Self::AF_IUCV => Some("AF_IUCV"),
            Self::AF_RXRPC => Some("AF_RXRPC"),
            Self::AF_ISDN => Some("AF_ISDN"),
            Self::AF_PHONET => Some("AF_PHONET"),
            Self::AF_IEEE802154 => Some("AF_IEEE802154"),
            Self::AF_CAIF => Some("AF_CAIF"),
            Self::AF_ALG => Some("AF_ALG"),
            Self::AF_NFC => Some("AF_NFC"),
            Self::AF_VSOCK => Some("AF_VSOCK"),
            Self::AF_KCM => Some("AF_KCM"),
            Self::AF_QIPCRTR => Some("AF_QIPCRTR"),
            Self::AF_SMC => Some("AF_SMC"),
            Self::AF_XDP => Some("AF_XDP"),
            Self::AF_MCTP => Some("AF_MCTP"),
            _ => None,
        }
    }
}

impl core::fmt::Display for AddressFamily {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match self.to_str() {
            Some(s) => formatter.write_str(s),
            None => write!(formatter, "(unknown socket family {})", self.0),
        }
    }
}

impl core::fmt::Debug for AddressFamily {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match self.to_str() {
            Some(s) => write!(formatter, "AddressFamily::{s}"),
            None => write!(formatter, "AddressFamily::<{}>", self.0),
        }
    }
}

impl From<AddressFamily> for sa_family_t {
    #[inline]
    fn from(val: AddressFamily) -> Self {
        val.val()
    }
}

impl From<sa_family_t> for AddressFamily {
    #[inline]
    fn from(val: sa_family_t) -> Self {
        Self::new(val)
    }
}
