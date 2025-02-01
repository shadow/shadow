use std::{
    marker::PhantomData,
    sync::atomic::{AtomicU32, Ordering},
};

use once_cell::sync::OnceCell;
use vasi::VirtualAddressSpaceIndependent;

pub mod cell;
pub mod rc;
pub mod refcell;

/// Every object root is assigned a [Tag], which we ensure is globally unique.
/// Each [Tag] value uniquely identifies a [Root].
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash, VirtualAddressSpaceIndependent)]
// Ensure consistent layout, since we use it in shared memory.
#[repr(C)]
pub struct Tag {
    // Intended to be unique on a machine. We use a random number here.
    global_id: TagGlobalId,
    // Only unique within a process. We *could* just use global_id, and perhaps
    // make it bigger, but having a local_id that we increment from 0 might be
    // helpful for debugging.
    local_id: TagLocalId,
}

/// Larger sizes here reduce the chance of collision, which could lead to
/// silently missing bugs in some cases. Note though that there would both
/// have to be a collision, and the code would need to incorrectly try to
/// access data using the wrong root lock.
///
/// Increasing the size introduces some runtime overhead for storing, copying,
/// and comparing tag values.
type TagGlobalId = u32;

/// Larger sizes here support a greater number of tags within a given prefix.
///
/// Increasing the size introduces some runtime overhead for storing, copying,
/// and comparing tag values.
type TagLocalId = u32;
type TagLocallyUniquePartAtomicType = AtomicU32;

impl Tag {
    pub fn new() -> Self {
        // Every instance of this module uses a random prefix for tags. This is
        // primarily to handle the case where this module is used from multiple
        // processes that share memory. We could alternatively use the pid here,
        // but that may open us up to more corner cases that could cause
        // collisions - e.g. pid namespaces, pid reuse, or multiple instances of
        // this module ending up in a single process due to dependencies
        // requiring different versions
        // https://doc.rust-lang.org/cargo/reference/resolver.html#semver-compatibility.
        static TAG_PREFIX: OnceCell<TagGlobalId> = OnceCell::new();
        let prefix = *TAG_PREFIX.get_or_init(rand::random);

        static NEXT_TAG_SUFFIX: TagLocallyUniquePartAtomicType =
            TagLocallyUniquePartAtomicType::new(0);
        let suffix: TagLocalId = NEXT_TAG_SUFFIX.fetch_add(1, Ordering::Relaxed);

        // Detect overflow
        assert!(suffix != TagLocalId::MAX);

        Self {
            global_id: prefix,
            local_id: suffix,
        }
    }
}

impl Default for Tag {
    fn default() -> Self {
        Self::new()
    }
}

/// A [Root] is a `![Sync]` token. Proof of access to a [Root] is used
/// to inexpensively ensure safety of safety in [rc::RootedRc] and
/// [refcell::RootedRefCell].
#[derive(Debug, VirtualAddressSpaceIndependent)]
// Ensure consistent layout, since this is an Archive type.
#[repr(C)]
pub struct Root {
    tag: Tag,
    _notsync: std::marker::PhantomData<std::cell::Cell<()>>,
}

impl Root {
    pub fn new() -> Self {
        let tag = Tag::new();
        Self {
            tag,
            _notsync: PhantomData,
        }
    }

    /// This root's globally unique tag.
    fn tag(&self) -> Tag {
        self.tag
    }
}

impl Default for Root {
    fn default() -> Self {
        Self::new()
    }
}
