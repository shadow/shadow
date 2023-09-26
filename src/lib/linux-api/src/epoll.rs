use crate::{bindings, const_conversions};
use num_enum::{IntoPrimitive, TryFromPrimitive};

bitflags::bitflags! {
    /// Epoll create flags, as used with `epoll_create1`.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct EpollCreateFlags: i32 {
        const EPOLL_CLOEXEC = const_conversions::i32_from_u32(bindings::LINUX_EPOLL_CLOEXEC);
    }
}

/// Epoll control operation, as used with `epoll_ctl`.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum EpollCtlOp {
    EPOLL_CTL_ADD = const_conversions::i32_from_u32(bindings::LINUX_EPOLL_CTL_ADD),
    EPOLL_CTL_MOD = const_conversions::i32_from_u32(bindings::LINUX_EPOLL_CTL_MOD),
    EPOLL_CTL_DEL = const_conversions::i32_from_u32(bindings::LINUX_EPOLL_CTL_DEL),
}

bitflags::bitflags! {
    /// Epoll event types and input flags, which are ORed together in the `events` member of
    /// `struct epoll_event`. As explained in `epoll_ctl(2)`, some flags represent event
    /// types and other flags specify various input wakeup options.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct EpollEvents: u32 {
        /// An epoll event type.
        const EPOLLIN = bindings::LINUX_EPOLLIN;
        /// An epoll event type.
        const EPOLLPRI = bindings::LINUX_EPOLLPRI;
        /// An epoll event type.
        const EPOLLOUT = bindings::LINUX_EPOLLOUT;
        /// An epoll event type.
        const EPOLLERR = bindings::LINUX_EPOLLERR;
        /// An epoll event type.
        const EPOLLHUP = bindings::LINUX_EPOLLHUP;
        /// An epoll event type.
        const EPOLLNVAL = bindings::LINUX_EPOLLNVAL;
        /// An epoll event type.
        const EPOLLRDNORM = bindings::LINUX_EPOLLRDNORM;
        /// An epoll event type.
        const EPOLLRDBAND = bindings::LINUX_EPOLLRDBAND;
        /// An epoll event type.
        const EPOLLWRNORM = bindings::LINUX_EPOLLWRNORM;
        /// An epoll event type.
        const EPOLLWRBAND = bindings::LINUX_EPOLLWRBAND;
        /// An epoll event type.
        const EPOLLMSG = bindings::LINUX_EPOLLMSG;
        /// An epoll event type.
        const EPOLLRDHUP = bindings::LINUX_EPOLLRDHUP;
        /// An epoll wakeup option.
        const EPOLLEXCLUSIVE = bindings::LINUX_EPOLLEXCLUSIVE;
        /// An epoll wakeup option.
        const EPOLLWAKEUP = bindings::LINUX_EPOLLWAKEUP;
        /// An epoll wakeup option.
        const EPOLLONESHOT = bindings::LINUX_EPOLLONESHOT;
        /// An epoll wakeup option.
        const EPOLLET = bindings::LINUX_EPOLLET;
    }
}

// The `epoll_event` struct is passed as an argument to `epoll_ctl` and `epoll_wait`.
#[allow(non_camel_case_types)]
pub type epoll_event = crate::bindings::linux_epoll_event;
unsafe impl shadow_pod::Pod for epoll_event {}
