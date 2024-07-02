use crate::bindings;

pub use bindings::linux_stat;
#[allow(non_camel_case_types)]
pub type stat = linux_stat;
unsafe impl shadow_pod::Pod for stat {}

bitflags::bitflags! {
    /// Stat flags, as used e.g. with `stat`.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct SFlag: u32 {
        const S_IFMT = bindings::LINUX_S_IFMT;
        const S_IFSOCK = bindings::LINUX_S_IFSOCK;
        const S_IFLNK = bindings::LINUX_S_IFLNK;
        const S_IFREG = bindings::LINUX_S_IFREG;
        const S_IFBLK = bindings::LINUX_S_IFBLK;
        const S_IFDIR = bindings::LINUX_S_IFDIR;
        const S_IFCHR = bindings::LINUX_S_IFCHR;
        const S_IFIFO = bindings::LINUX_S_IFIFO;
        const S_ISUID = bindings::LINUX_S_ISUID;
        const S_ISGID = bindings::LINUX_S_ISGID;
        const S_ISVTX = bindings::LINUX_S_ISVTX;
        const S_IRWXU = bindings::LINUX_S_IRWXU;
        const S_IRUSR = bindings::LINUX_S_IRUSR;
        const S_IWUSR = bindings::LINUX_S_IWUSR;
        const S_IXUSR = bindings::LINUX_S_IXUSR;
        const S_IRWXG = bindings::LINUX_S_IRWXG;
        const S_IRGRP = bindings::LINUX_S_IRGRP;
        const S_IWGRP = bindings::LINUX_S_IWGRP;
        const S_IXGRP = bindings::LINUX_S_IXGRP;
        const S_IRWXO = bindings::LINUX_S_IRWXO;
        const S_IROTH = bindings::LINUX_S_IROTH;
        const S_IWOTH = bindings::LINUX_S_IWOTH;
        const S_IXOTH = bindings::LINUX_S_IXOTH;
    }
}
