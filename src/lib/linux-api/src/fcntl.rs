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
        const O_NDELAY = const_conversions::i32_from_u32(bindings::LINUX_O_NDELAY);
        const O_ASYNC = const_conversions::i32_from_u32(bindings::LINUX_FASYNC);
    }
}

impl OFlag {
    pub fn access_mode_flags(&self) -> AccessModeOFlag {
        AccessModeOFlag::from_bits_truncate(self.bits())
    }

    pub fn file_creation_flags(&self) -> FileCreationOFlag {
        FileCreationOFlag::from_bits_truncate(self.bits())
    }

    pub fn file_status_flags(&self) -> FileStatusOFlag {
        FileStatusOFlag::from_bits_truncate(self.bits())
    }

    /// Split into access-mode, creation, and status flags.
    pub fn partition(&self) -> (AccessModeOFlag, FileCreationOFlag, FileStatusOFlag) {
        (
            self.access_mode_flags(),
            self.file_creation_flags(),
            self.file_status_flags(),
        )
    }
}

impl From<AccessModeOFlag> for OFlag {
    fn from(value: AccessModeOFlag) -> Self {
        Self::from_bits(value.bits()).unwrap()
    }
}

impl From<FileCreationOFlag> for OFlag {
    fn from(value: FileCreationOFlag) -> Self {
        Self::from_bits(value.bits()).unwrap()
    }
}

impl From<FileStatusOFlag> for OFlag {
    fn from(value: FileStatusOFlag) -> Self {
        Self::from_bits(value.bits()).unwrap()
    }
}

bitflags::bitflags! {
    /// "Access mode" flags as specified in open(2); Subset of OFlag.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct AccessModeOFlag: i32 {
        const O_RDONLY = OFlag::O_RDONLY.bits();
        const O_WRONLY = OFlag::O_WRONLY.bits();
        const O_RDWR = OFlag::O_RDWR.bits();
    }
}

bitflags::bitflags! {
    /// "File creation flags" as specified in open(2). Subset of OFlag.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct FileCreationOFlag : i32 {
        const O_CLOEXEC = OFlag::O_CLOEXEC.bits();
        const O_CREAT= OFlag::O_CREAT.bits();
        const O_DIRECTORY= OFlag::O_DIRECTORY.bits();
        const O_EXCL= OFlag::O_EXCL.bits();
        const O_NOCTTY= OFlag::O_NOCTTY.bits();
        const O_NOFOLLOW= OFlag::O_NOFOLLOW.bits();
        const O_TMPFILE= OFlag::O_TMPFILE.bits();
        const O_TRUNC= OFlag::O_TRUNC.bits();
    }
}

bitflags::bitflags! {
    /// "File status flags" as specified in open(2), as "all the remaining flags"
    /// that aren't specified as creation or access flags.
    ///
    /// open(2): "The file status flags can be retrieved and (in some cases)
    /// modified; see fcntl(2) for details."
    ///
    /// Subset of OFlag.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct FileStatusOFlag: i32 {
        const O_APPEND= OFlag::O_APPEND.bits();
        const O_ASYNC= OFlag::O_ASYNC.bits();
        const O_DIRECT= OFlag::O_DIRECT.bits();
        const O_NOATIME= OFlag::O_NOATIME.bits();
        const O_NONBLOCK= OFlag::O_NONBLOCK.bits();
        const O_DSYNC= OFlag::O_DSYNC.bits();
        const O_SYNC= OFlag::O_SYNC.bits();
        const O_LARGEFILE= OFlag::O_LARGEFILE.bits();
        const O_PATH= OFlag::O_PATH.bits();
    }
}

/// fcntl commands, as used with `fcntl`.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum FcntlCommand {
    F_DUPFD = bindings::LINUX_F_DUPFD,
    F_GETFD = bindings::LINUX_F_GETFD,
    F_SETFD = bindings::LINUX_F_SETFD,
    F_GETFL = bindings::LINUX_F_GETFL,
    F_SETFL = bindings::LINUX_F_SETFL,
    F_GETLK = bindings::LINUX_F_GETLK,
    F_SETLK = bindings::LINUX_F_SETLK,
    F_SETLKW = bindings::LINUX_F_SETLKW,
    F_SETOWN = bindings::LINUX_F_SETOWN,
    F_GETOWN = bindings::LINUX_F_GETOWN,
    F_SETSIG = bindings::LINUX_F_SETSIG,
    F_GETSIG = bindings::LINUX_F_GETSIG,
    F_SETOWN_EX = bindings::LINUX_F_SETOWN_EX,
    F_GETOWN_EX = bindings::LINUX_F_GETOWN_EX,
    F_GETOWNER_UIDS = bindings::LINUX_F_GETOWNER_UIDS,
    F_OFD_GETLK = bindings::LINUX_F_OFD_GETLK,
    F_OFD_SETLK = bindings::LINUX_F_OFD_SETLK,
    F_OFD_SETLKW = bindings::LINUX_F_OFD_SETLKW,
    F_SETLEASE = bindings::LINUX_F_SETLEASE,
    F_GETLEASE = bindings::LINUX_F_GETLEASE,
    F_NOTIFY = bindings::LINUX_F_NOTIFY,
    F_DUPFD_QUERY = bindings::LINUX_F_DUPFD_QUERY,
    F_CREATED_QUERY = bindings::LINUX_F_CREATED_QUERY,
    F_DUPFD_CLOEXEC = bindings::LINUX_F_DUPFD_CLOEXEC,
    F_SETPIPE_SZ = bindings::LINUX_F_SETPIPE_SZ,
    F_GETPIPE_SZ = bindings::LINUX_F_GETPIPE_SZ,
    F_ADD_SEALS = bindings::LINUX_F_ADD_SEALS,
    F_GET_SEALS = bindings::LINUX_F_GET_SEALS,
    F_CANCELLK = bindings::LINUX_F_CANCELLK,
    F_GET_RW_HINT = bindings::LINUX_F_GET_RW_HINT,
    F_SET_RW_HINT = bindings::LINUX_F_SET_RW_HINT,
    F_GET_FILE_RW_HINT = bindings::LINUX_F_GET_FILE_RW_HINT,
    F_SET_FILE_RW_HINT = bindings::LINUX_F_SET_FILE_RW_HINT,
    F_GETDELEG = bindings::LINUX_F_GETDELEG,
    F_SETDELEG = bindings::LINUX_F_SETDELEG,
}

pub use bindings::linux_flock as flock;
unsafe impl shadow_pod::Pod for flock {}

// Valid values for `flock::l_whence`.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i16)]
#[allow(non_camel_case_types)]
pub enum FlockWhence {
    SEEK_SET = const_conversions::i16_from_u32(bindings::LINUX_SEEK_SET),
    SEEK_CUR = const_conversions::i16_from_u32(bindings::LINUX_SEEK_CUR),
    SEEK_END = const_conversions::i16_from_u32(bindings::LINUX_SEEK_END),
}

/// Owner, as used with [`FcntlCommand::F_SETOWN_EX`] and [`FcntlCommand::F_GETOWN_EX`]
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum FcntlOwner {
    F_OWNER_TID = bindings::LINUX_F_OWNER_TID,
    F_OWNER_PID = bindings::LINUX_F_OWNER_PID,
    F_OWNER_PGRP = bindings::LINUX_F_OWNER_PGRP,
}

/// Lease type, as used with [`FcntlCommand::F_SETLEASE`]
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum FcntlLeaseType {
    F_RDLCK = bindings::LINUX_F_RDLCK,
    F_WRLCK = bindings::LINUX_F_WRLCK,
    F_UNLCK = bindings::LINUX_F_UNLCK,
    F_EXLCK = bindings::LINUX_F_EXLCK,
    F_SHLCK = bindings::LINUX_F_SHLCK,
}

/// Lock type, as found in `flock::l_type`, used with e.g.
/// [`FcntlCommand::F_SETLK`] and [`FcntlCommand::F_OFD_SETLK`]
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i16)]
#[allow(non_camel_case_types)]
pub enum FlockType {
    F_RDLCK = const_conversions::i16_from_u32(bindings::LINUX_F_RDLCK),
    F_WRLCK = const_conversions::i16_from_u32(bindings::LINUX_F_WRLCK),
    F_UNLCK = const_conversions::i16_from_u32(bindings::LINUX_F_UNLCK),
}

/// Seal type, as used with [`FcntlCommand::F_ADD_SEALS`] and [`FcntlCommand::F_GET_SEALS`].
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum FcntlSealType {
    F_SEAL_SEAL = bindings::LINUX_F_SEAL_SEAL,
    F_SEAL_SHRINK = bindings::LINUX_F_SEAL_SHRINK,
    F_SEAL_GROW = bindings::LINUX_F_SEAL_GROW,
    F_SEAL_WRITE = bindings::LINUX_F_SEAL_WRITE,
    F_SEAL_FUTURE_WRITE = bindings::LINUX_F_SEAL_FUTURE_WRITE,
    F_SEAL_EXEC = bindings::LINUX_F_SEAL_EXEC,
}

/// Read-write hint, as used with [`FcntlCommand::F_GET_RW_HINT`] and [`FcntlCommand::F_SET_RW_HINT`].
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum FcntlRwHint {
    RWH_WRITE_LIFE_NOT_SET = bindings::LINUX_RWH_WRITE_LIFE_NOT_SET,
    RWH_WRITE_LIFE_NONE = bindings::LINUX_RWH_WRITE_LIFE_NONE,
    RWH_WRITE_LIFE_SHORT = bindings::LINUX_RWH_WRITE_LIFE_SHORT,
    RWH_WRITE_LIFE_MEDIUM = bindings::LINUX_RWH_WRITE_LIFE_MEDIUM,
    RWH_WRITE_LIFE_LONG = bindings::LINUX_RWH_WRITE_LIFE_LONG,
    RWH_WRITE_LIFE_EXTREME = bindings::LINUX_RWH_WRITE_LIFE_EXTREME,
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

bitflags::bitflags! {
    /// flags for execveat.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct ExecveAtFlags: i32 {
        const AT_EMPTY_PATH = const_conversions::i32_from_u32(bindings::LINUX_AT_EMPTY_PATH);
        const AT_SYMLINK_NOFOLLOW = const_conversions::i32_from_u32(bindings::LINUX_AT_SYMLINK_NOFOLLOW);
        const AT_EXECVE_CHECK = const_conversions::i32_from_u32(bindings::LINUX_AT_EXECVE_CHECK);
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_partition_oflags() {
        let all_o_flags = OFlag::all();
        let (access_mode, creation, status) = all_o_flags.partition();

        // all defined bits should be set in partitions.
        assert_eq!(access_mode, AccessModeOFlag::all());
        assert_eq!(creation, FileCreationOFlag::all());
        assert_eq!(status, FileStatusOFlag::all());

        // convert back to OFlag.
        let access_mode = OFlag::from(access_mode);
        let creation = OFlag::from(creation);
        let status = OFlag::from(status);

        // they should all be mutually exclusive.
        assert_eq!(access_mode & creation, OFlag::empty());
        assert_eq!(access_mode & status, OFlag::empty());
        assert_eq!(creation & status, OFlag::empty());

        // together they should cover all of the OFlag bits.
        assert_eq!(
            OFlag::all() - (access_mode | creation | status),
            OFlag::empty()
        );
    }
}

mod export {
    /// "file creation" "oflags", as per `open(2)`.
    // TODO: export as a plain constant, if/when cbindgen handles evaluation of
    // const functions.
    #[unsafe(no_mangle)]
    pub extern "C-unwind" fn linux_file_creation_oflags() -> core::ffi::c_int {
        super::FileCreationOFlag::all().bits()
    }

    /// "access mode" "oflags", as per `open(2)`.
    // TODO: export as a plain constant, if/when cbindgen handles evaluation of
    // const functions.
    #[unsafe(no_mangle)]
    pub extern "C-unwind" fn linux_access_mode_oflags() -> core::ffi::c_int {
        super::AccessModeOFlag::all().bits()
    }

    /// "file status" "oflags", as per `open(2)`.
    // TODO: export as a plain constant, if/when cbindgen handles evaluation of
    // const functions.
    #[unsafe(no_mangle)]
    pub extern "C-unwind" fn linux_file_status_oflags() -> core::ffi::c_int {
        super::FileStatusOFlag::all().bits()
    }
}
