use num_enum::IntoPrimitive;
use num_enum::TryFromPrimitive;

use crate::bindings;
use crate::const_conversions;

bitflags::bitflags! {
    #[repr(transparent)]
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct WaitFlags: i32 {
        const WNOHANG = const_conversions::i32_from_u32(bindings::LINUX_WNOHANG);
        const WUNTRACED = const_conversions::i32_from_u32(bindings::LINUX_WUNTRACED);
        const WEXITED   = const_conversions::i32_from_u32(bindings::LINUX_WEXITED  );
        const WCONTINUED  = const_conversions::i32_from_u32(bindings::LINUX_WCONTINUED );
        const WNOWAIT   = const_conversions::i32_from_u32(bindings::LINUX_WNOWAIT  );
        const WSTOPPED = const_conversions::i32_from_u32(bindings::LINUX_WSTOPPED);

        const __WNOTHREAD = const_conversions::i32_from_u32(bindings::LINUX___WNOTHREAD);
        const __WALL    = const_conversions::i32_from_u32(bindings::LINUX___WALL   );
        const __WCLONE  = const_conversions::i32_from_u32_allowing_wraparound(bindings::LINUX___WCLONE );
    }
}

#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, TryFromPrimitive, IntoPrimitive)]
pub enum WaitId {
    P_ALL = const_conversions::i32_from_u32(bindings::LINUX_P_ALL),
    P_PID = const_conversions::i32_from_u32(bindings::LINUX_P_PID),
    P_PGID = const_conversions::i32_from_u32(bindings::LINUX_P_PGID),
    P_PIDFD = const_conversions::i32_from_u32(bindings::LINUX_P_PIDFD),
}
