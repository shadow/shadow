use std::io::Cursor;

use neli::consts::nl::{NlmF, NlmFFlags};
use neli::consts::rtnl::{IfaFFlags, RtAddrFamily, RtScope, Rtm};
use neli::nl::{NlPayload, Nlmsghdr};
use neli::rtnl::Ifaddrmsg;
use neli::types::RtBuffer;
use neli::ToBytes;

use test_utils::{set, ShadowTest, TestEnvironment};

// Send a bunch of Netlink messages to the file descriptor. Return the number of written bytes.
fn flood(fd: libc::c_int) -> libc::ssize_t {
    let ifaddrmsg = Ifaddrmsg {
        ifa_family: RtAddrFamily::Unspecified,
        ifa_prefixlen: 0,
        ifa_flags: IfaFFlags::empty(),
        ifa_scope: RtScope::Universe.into(),
        ifa_index: 0,
        rtattrs: RtBuffer::new(),
    };
    let nlmsg = {
        let len = None;
        let nl_type = Rtm::Getaddr;
        let flags = NlmFFlags::new(&[NlmF::Request, NlmF::Dump]);
        let seq = Some(0xfe182ab9); // Random number
        let pid = None;
        let payload = NlPayload::Payload(ifaddrmsg);
        Nlmsghdr::new(len, nl_type, flags, seq, pid, payload)
    };

    let mut buffer = Cursor::new(Vec::new());
    nlmsg.to_bytes(&mut buffer).unwrap();
    let buffer = buffer.into_inner();

    let mut written = 0;
    // Send about 8KB of data
    for _ in 0..8192 / buffer.len() {
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
            return ret;
        } else {
            written += ret;
        }
    }
    written
}

fn test_send_limit_not_exceed() -> anyhow::Result<()> {
    let fd = unsafe {
        libc::socket(
            libc::AF_NETLINK,
            libc::SOCK_RAW | libc::SOCK_NONBLOCK,
            libc::NETLINK_ROUTE,
        )
    };
    assert!(fd >= 0);
    assert!(flood(fd) >= 0);
    Ok(())
}

fn test_send_limit_exceed() -> anyhow::Result<()> {
    let fd = unsafe {
        libc::socket(
            libc::AF_NETLINK,
            libc::SOCK_RAW | libc::SOCK_NONBLOCK,
            libc::NETLINK_ROUTE,
        )
    };
    assert!(fd >= 0);

    let limit_bytes = 2048u32.to_le_bytes();
    test_utils::check_system_call!(
        move || unsafe {
            libc::setsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_SNDBUF,
                limit_bytes.as_ptr() as *const core::ffi::c_void,
                limit_bytes.len().try_into().unwrap(),
            )
        },
        &[],
    )
    .unwrap();

    assert!(flood(fd) < 0);
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
        ShadowTest::new(
            "send-limit-not-exceed",
            test_send_limit_not_exceed,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "send-limit-exceed",
            test_send_limit_exceed,
            // The send limit exceed error does not occur in Linux. I don't know why.
            set![TestEnvironment::Shadow],
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
