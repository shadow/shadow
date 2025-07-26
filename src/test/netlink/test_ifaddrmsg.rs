use std::io::Cursor;

use anyhow::anyhow;
use neli::consts::nl::NlmF;
use neli::consts::rtnl::{RtAddrFamily, RtScope, Rtm};
use neli::nl::{NlPayload, Nlmsghdr, NlmsghdrBuilder};
use neli::rtnl::{Ifaddrmsg, IfaddrmsgBuilder};
use neli::{FromBytes, ToBytes};

use test_utils::{ShadowTest, TestEnvironment, set};

fn test_normal() -> anyhow::Result<()> {
    let fd = unsafe {
        libc::socket(
            libc::AF_NETLINK,
            libc::SOCK_RAW | libc::SOCK_NONBLOCK,
            libc::NETLINK_ROUTE,
        )
    };

    let ifaddrmsg = IfaddrmsgBuilder::default()
        .ifa_family(RtAddrFamily::Unspecified)
        .ifa_prefixlen(0)
        .ifa_scope(RtScope::Universe)
        .ifa_index(0)
        .build()
        .unwrap();
    let nlmsg = NlmsghdrBuilder::default()
        .nl_type(Rtm::Getaddr)
        .nl_flags(NlmF::REQUEST | NlmF::DUMP)
        .nl_seq(0xfe182ab9) // Random number
        .nl_payload(NlPayload::Payload(ifaddrmsg))
        .build()
        .unwrap();

    let mut buffer = Cursor::new(Vec::new());
    nlmsg.to_bytes(&mut buffer).unwrap();
    let buffer = buffer.into_inner();

    let ret = unsafe {
        libc::sendto(
            fd,
            buffer.as_ptr() as *const core::ffi::c_void,
            buffer.len(),
            0,
            core::ptr::null(),
            0,
        )
    };
    if ret < 0 {
        return Err(anyhow!("sento error"));
    }

    let mut buffer = vec![0; 1024];
    let ret = unsafe {
        libc::recvfrom(
            fd,
            buffer.as_mut_ptr() as *mut core::ffi::c_void,
            buffer.len(),
            0,
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    };
    if ret < 0 {
        return Err(anyhow!("recvfrom error"));
    }
    buffer.truncate(ret as usize);

    let Ok(nlmsg) = Nlmsghdr::<Rtm, Ifaddrmsg>::from_bytes(&mut Cursor::new(buffer.as_slice()))
    else {
        return Err(anyhow!("failed to deserialize the message"));
    };
    let Some(_ifaddrmsg) = nlmsg.get_payload() else {
        return Err(anyhow!("failed to find the payload"));
    };

    Ok(())
}

fn test_shorter_than_nlmsghdr() -> anyhow::Result<()> {
    let fd = unsafe {
        libc::socket(
            libc::AF_NETLINK,
            libc::SOCK_RAW | libc::SOCK_NONBLOCK,
            libc::NETLINK_ROUTE,
        )
    };

    let ifaddrmsg = IfaddrmsgBuilder::default()
        .ifa_family(RtAddrFamily::Unspecified)
        .ifa_prefixlen(0)
        .ifa_scope(RtScope::Universe)
        .ifa_index(0)
        .build()
        .unwrap();
    let nlmsg = NlmsghdrBuilder::default()
        .nl_type(Rtm::Getaddr)
        .nl_flags(NlmF::REQUEST | NlmF::DUMP)
        .nl_seq(0xfe182ab9) // Random number
        .nl_payload(NlPayload::Payload(ifaddrmsg))
        .build()
        .unwrap();

    let mut buffer = Cursor::new(Vec::new());
    nlmsg.to_bytes(&mut buffer).unwrap();
    let mut buffer = buffer.into_inner();
    // buffer is 15 bytes long which is shorter than nlmsghdr which is 16 bytes long
    buffer[0] = 15;
    buffer.truncate(15);

    let ret = unsafe {
        libc::sendto(
            fd,
            buffer.as_ptr() as *const core::ffi::c_void,
            buffer.len(),
            0,
            core::ptr::null(),
            0,
        )
    };
    if ret < 0 {
        return Err(anyhow!("sento error"));
    }

    let mut buffer = vec![0; 1024];
    let ret = unsafe {
        libc::recvfrom(
            fd,
            buffer.as_mut_ptr() as *mut core::ffi::c_void,
            buffer.len(),
            0,
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    };
    if ret < 0 {
        return Ok(());
    }
    Err(anyhow!("expected recvfrom error"))
}

fn test_shorter_than_ifaddrmsg() -> anyhow::Result<()> {
    let fd = unsafe {
        libc::socket(
            libc::AF_NETLINK,
            libc::SOCK_RAW | libc::SOCK_NONBLOCK,
            libc::NETLINK_ROUTE,
        )
    };

    let ifaddrmsg = IfaddrmsgBuilder::default()
        .ifa_family(RtAddrFamily::Unspecified)
        .ifa_prefixlen(0)
        .ifa_scope(RtScope::Universe)
        .ifa_index(0)
        .build()
        .unwrap();
    let nlmsg = NlmsghdrBuilder::default()
        .nl_type(Rtm::Getaddr)
        .nl_flags(NlmF::REQUEST | NlmF::DUMP)
        .nl_seq(0xfe182ab9) // Random number
        .nl_payload(NlPayload::Payload(ifaddrmsg))
        .build()
        .unwrap();

    let mut buffer = Cursor::new(Vec::new());
    nlmsg.to_bytes(&mut buffer).unwrap();
    let mut buffer = buffer.into_inner();
    // buffer is 17 bytes long which is shorter than nlmsghdr+ifaddrmsg which is 24 bytes long
    buffer[0] = 17;
    buffer.truncate(17);

    let ret = unsafe {
        libc::sendto(
            fd,
            buffer.as_ptr() as *const core::ffi::c_void,
            buffer.len(),
            0,
            core::ptr::null(),
            0,
        )
    };
    if ret < 0 {
        return Err(anyhow!("sento error"));
    }

    let mut buffer = vec![0; 1024];
    let ret = unsafe {
        libc::recvfrom(
            fd,
            buffer.as_mut_ptr() as *mut core::ffi::c_void,
            buffer.len(),
            0,
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    };
    if ret < 0 {
        return Err(anyhow!("recvfrom error"));
    }
    buffer.truncate(ret as usize);

    let Ok(nlmsg) = Nlmsghdr::<Rtm, Ifaddrmsg>::from_bytes(&mut Cursor::new(buffer.as_slice()))
    else {
        return Err(anyhow!("failed to deserialize the message"));
    };
    let Some(_ifaddrmsg) = nlmsg.get_payload() else {
        return Err(anyhow!("failed to find the payload"));
    };

    Ok(())
}

fn main() -> anyhow::Result<()> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnvironment::Libc, TestEnvironment::Shadow];

    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![
        ShadowTest::new("normal", test_normal, all_envs.clone()),
        ShadowTest::new(
            "shorter-than-nlmsghdr",
            test_shorter_than_nlmsghdr,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "shorter-than-ifaddrmsg",
            test_shorter_than_ifaddrmsg,
            all_envs.clone(),
        ),
    ];

    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnvironment::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnvironment::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}
