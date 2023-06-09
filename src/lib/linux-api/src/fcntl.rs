use num_enum::{IntoPrimitive, TryFromPrimitive};

use crate::{bindings, const_conversions};

bitflags::bitflags! {
    /// Open flags, as used e.g. with `open`.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct OFlag: i32 {
        const O_ACCMODE = const_conversions::i32_from_u32(bindings::LINUX_O_ACCMODE);
        const O_RDONLY = const_conversions::i32_from_u32(bindings::LINUX_O_RDONLY);
        const O_WRONLY = const_conversions::i32_from_u32(bindings::LINUX_O_WRONLY);
        const O_RDWR = const_conversions::i32_from_u32(bindings::LINUX_O_RDWR);
        const O_CREAT = const_conversions::i32_from_u32(bindings::LINUX_O_CREAT);
        const O_EXCL = const_conversions::i32_from_u32(bindings::LINUX_O_EXCL);
        const O_NOCTTY = const_conversions::i32_from_u32(bindings::LINUX_O_NOCTTY);
        const O_TRUNC = const_conversions::i32_from_u32(bindings::LINUX_O_TRUNC);
        const O_APPEND = const_conversions::i32_from_u32(bindings::LINUX_O_APPEND);
        const O_NONBLOCK = const_conversions::i32_from_u32(bindings::LINUX_O_NONBLOCK);
        const O_DSYNC = const_conversions::i32_from_u32(bindings::LINUX_O_DSYNC);
        const O_DIRECT = const_conversions::i32_from_u32(bindings::LINUX_O_DIRECT);
        const O_LARGEFILE = const_conversions::i32_from_u32(bindings::LINUX_O_LARGEFILE);
        const O_DIRECTORY = const_conversions::i32_from_u32(bindings::LINUX_O_DIRECTORY);
        const O_NOFOLLOW = const_conversions::i32_from_u32(bindings::LINUX_O_NOFOLLOW);
        const O_NOATIME = const_conversions::i32_from_u32(bindings::LINUX_O_NOATIME);
        const O_CLOEXEC = const_conversions::i32_from_u32(bindings::LINUX_O_CLOEXEC);
        const O_SYNC = const_conversions::i32_from_u32(bindings::LINUX_O_SYNC);
        const O_PATH = const_conversions::i32_from_u32(bindings::LINUX_O_PATH);
        const O_TMPFILE = const_conversions::i32_from_u32(bindings::LINUX_O_TMPFILE);
        const O_TMPFILE_MASK = const_conversions::i32_from_u32(bindings::LINUX_O_TMPFILE_MASK);
        const O_NDELAY = const_conversions::i32_from_u32(bindings::LINUX_O_NDELAY);
        const O_ASYNC = const_conversions::i32_from_u32(bindings::LINUX_FASYNC);
    }
}

/// fcntl commands, as used with `fcntl`.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum FcntlCommand {
    F_DUPFD = const_conversions::i32_from_u32(bindings::LINUX_F_DUPFD),
    F_GETFD = const_conversions::i32_from_u32(bindings::LINUX_F_GETFD),
    F_SETFD = const_conversions::i32_from_u32(bindings::LINUX_F_SETFD),
    F_GETFL = const_conversions::i32_from_u32(bindings::LINUX_F_GETFL),
    F_SETFL = const_conversions::i32_from_u32(bindings::LINUX_F_SETFL),
    F_GETLK = const_conversions::i32_from_u32(bindings::LINUX_F_GETLK),
    F_SETLK = const_conversions::i32_from_u32(bindings::LINUX_F_SETLK),
    F_SETLKW = const_conversions::i32_from_u32(bindings::LINUX_F_SETLKW),
    F_SETOWN = const_conversions::i32_from_u32(bindings::LINUX_F_SETOWN),
    F_GETOWN = const_conversions::i32_from_u32(bindings::LINUX_F_GETOWN),
    F_SETSIG = const_conversions::i32_from_u32(bindings::LINUX_F_SETSIG),
    F_GETSIG = const_conversions::i32_from_u32(bindings::LINUX_F_GETSIG),
    F_SETOWN_EX = const_conversions::i32_from_u32(bindings::LINUX_F_SETOWN_EX),
    F_GETOWN_EX = const_conversions::i32_from_u32(bindings::LINUX_F_GETOWN_EX),
    F_GETOWNER_UIDS = const_conversions::i32_from_u32(bindings::LINUX_F_GETOWNER_UIDS),
    F_OFD_GETLK = const_conversions::i32_from_u32(bindings::LINUX_F_OFD_GETLK),
    F_OFD_SETLK = const_conversions::i32_from_u32(bindings::LINUX_F_OFD_SETLK),
    F_OFD_SETLKW = const_conversions::i32_from_u32(bindings::LINUX_F_OFD_SETLKW),
    F_SETLEASE = const_conversions::i32_from_u32(bindings::LINUX_F_SETLEASE),
    F_GETLEASE = const_conversions::i32_from_u32(bindings::LINUX_F_GETLEASE),
    F_DUPFD_CLOEXEC = const_conversions::i32_from_u32(bindings::LINUX_F_DUPFD_CLOEXEC),
    F_NOTIFY = const_conversions::i32_from_u32(bindings::LINUX_F_NOTIFY),
    F_SETPIPE_SZ = const_conversions::i32_from_u32(bindings::LINUX_F_SETPIPE_SZ),
    F_GETPIPE_SZ = const_conversions::i32_from_u32(bindings::LINUX_F_GETPIPE_SZ),
    F_ADD_SEALS = const_conversions::i32_from_u32(bindings::LINUX_F_ADD_SEALS),
    F_GET_SEALS = const_conversions::i32_from_u32(bindings::LINUX_F_GET_SEALS),
    F_CANCELLK = const_conversions::i32_from_u32(bindings::LINUX_F_CANCELLK),
    F_GET_RW_HINT = const_conversions::i32_from_u32(bindings::LINUX_F_GET_RW_HINT),
    F_SET_RW_HINT = const_conversions::i32_from_u32(bindings::LINUX_F_SET_RW_HINT),
    F_GET_FILE_RW_HINT = const_conversions::i32_from_u32(bindings::LINUX_F_GET_FILE_RW_HINT),
    F_SET_FILE_RW_HINT = const_conversions::i32_from_u32(bindings::LINUX_F_SET_FILE_RW_HINT),
}

/// Owner, as used with [`FcntlCommand::F_SETOWN_EX`] and [`FcntlCommand::F_GETOWN_EX`]
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum FcntlOwner {
    F_OWNER_TID = const_conversions::i32_from_u32(bindings::LINUX_F_OWNER_TID),
    F_OWNER_PID = const_conversions::i32_from_u32(bindings::LINUX_F_OWNER_PID),
    F_OWNER_PGRP = const_conversions::i32_from_u32(bindings::LINUX_F_OWNER_PGRP),
}

/// Lease type, as used with [`FcntlCommand::F_SETLEASE`]
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum FcntlLeaseType {
    F_RDLCK = const_conversions::i32_from_u32(bindings::LINUX_F_RDLCK),
    F_WRLCK = const_conversions::i32_from_u32(bindings::LINUX_F_WRLCK),
    F_UNLCK = const_conversions::i32_from_u32(bindings::LINUX_F_UNLCK),
    F_EXLCK = const_conversions::i32_from_u32(bindings::LINUX_F_EXLCK),
    F_SHLCK = const_conversions::i32_from_u32(bindings::LINUX_F_SHLCK),
}

/// Seal type, as used with [`FcntlCommand::F_ADD_SEALS`] and [`FcntlCommand::F_GET_SEALS`].
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum FcntlSealType {
    F_SEAL_SEAL = const_conversions::i32_from_u32(bindings::LINUX_F_SEAL_SEAL),
    F_SEAL_SHRINK = const_conversions::i32_from_u32(bindings::LINUX_F_SEAL_SHRINK),
    F_SEAL_GROW = const_conversions::i32_from_u32(bindings::LINUX_F_SEAL_GROW),
    F_SEAL_WRITE = const_conversions::i32_from_u32(bindings::LINUX_F_SEAL_WRITE),
    F_SEAL_FUTURE_WRITE = const_conversions::i32_from_u32(bindings::LINUX_F_SEAL_FUTURE_WRITE),
}

/// Read-write hint, as used with [`FcntlCommand::F_GET_RW_HINT`] and [`FcntlCommand::F_SET_RW_HINT`].
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum FcntlRwHint {
    RWH_WRITE_LIFE_NOT_SET =
        const_conversions::i32_from_u32(bindings::LINUX_RWH_WRITE_LIFE_NOT_SET),
    RWH_WRITE_LIFE_NONE = const_conversions::i32_from_u32(bindings::LINUX_RWH_WRITE_LIFE_NONE),
    RWH_WRITE_LIFE_SHORT = const_conversions::i32_from_u32(bindings::LINUX_RWH_WRITE_LIFE_SHORT),
    RWH_WRITE_LIFE_MEDIUM = const_conversions::i32_from_u32(bindings::LINUX_RWH_WRITE_LIFE_MEDIUM),
    RWH_WRITE_LIFE_LONG = const_conversions::i32_from_u32(bindings::LINUX_RWH_WRITE_LIFE_LONG),
    RWH_WRITE_LIFE_EXTREME =
        const_conversions::i32_from_u32(bindings::LINUX_RWH_WRITE_LIFE_EXTREME),
}

bitflags::bitflags! {
    /// Descriptor flags, as used with [`FcntlCommand::F_GETFL`] and [`FcntlCommand::F_SETFL`].
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct DescriptorFlags: i32 {
        const FD_CLOEXEC = const_conversions::i32_from_u32(bindings::LINUX_FD_CLOEXEC);
    }
}

impl DescriptorFlags {
    pub fn as_o_flags(&self) -> OFlag {
        let mut flags = OFlag::empty();
        if self.contains(Self::FD_CLOEXEC) {
            flags.insert(OFlag::O_CLOEXEC);
        }
        flags
    }

    /// Returns a tuple of the `DescriptorFlags` and any remaining flags.
    pub fn from_o_flags(flags: OFlag) -> (Self, OFlag) {
        let mut remaining = flags;
        let mut flags = Self::empty();

        if remaining.contains(OFlag::O_CLOEXEC) {
            remaining.remove(OFlag::O_CLOEXEC);
            flags.insert(Self::FD_CLOEXEC);
        }

        (flags, remaining)
    }
}
