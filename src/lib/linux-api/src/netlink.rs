#[allow(non_camel_case_types)]
pub type nlmsghdr = crate::bindings::linux_nlmsghdr;
unsafe impl shadow_pod::Pod for nlmsghdr {}

#[allow(non_camel_case_types)]
pub type ifaddrmsg = crate::bindings::linux_ifaddrmsg;
unsafe impl shadow_pod::Pod for ifaddrmsg {}

#[allow(non_camel_case_types)]
pub type ifinfomsg = crate::bindings::linux_ifinfomsg;
unsafe impl shadow_pod::Pod for ifinfomsg {}
