use std::{
    marker::PhantomData,
    sync::atomic::{AtomicU32, Ordering},
};

use once_cell::sync::OnceCell;

/// Every object root is assigned a [Tag], which we ensure is globally unique.
/// Each [Tag] value uniquely identifies a [Root].
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
struct Tag {
    prefix: TagPrefixType,
    suffix: TagSuffixType,
}

/// Larger sizes here reduce the chance of collision, which could lead to
/// silently missing bugs in some cases. Note though that there would both
/// have to be a collision, and the code would need to incorrectly try to
/// access data using the wrong root lock.
///
/// Increasing the size introduces some runtime overhead for storing, copying,
/// and comparing tag values.
type TagPrefixType = u32;

/// Larger sizes here support a greater number of tags within a given prefix.
///
/// Increasing the size introduces some runtime overhead for storing, copying,
/// and comparing tag values.
type TagSuffixType = u32;
type TagSuffixAtomicType = AtomicU32;

impl Tag {
    pub fn new() -> Self {
        // Every instance of this module uses a random prefix for tags.  This is to
        // handle both the case where this module is used from multiple processes that
        // share memory, and to handle the case where multiple instances of this module
        // end up within a single process.
        static TAG_PREFIX: OnceCell<TagPrefixType> = OnceCell::new();
        let prefix = *TAG_PREFIX.get_or_init(rand::prelude::random);

        static NEXT_TAG_SUFFIX: TagSuffixAtomicType = TagSuffixAtomicType::new(0);
        let suffix: TagSuffixType = NEXT_TAG_SUFFIX.fetch_add(1, Ordering::Relaxed);

        // Detect overflow
        assert!(suffix != TagSuffixType::MAX);

        Self { prefix, suffix }
    }
}

/// [Root] is a `!Sync` token. [rc::RootedRc] and [refcell::RootedRefCell] use
/// it to prove no other threads currently have access to their resources.
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

pub mod rc;
pub mod refcell;
