use crate::utility::counter::Counter;

use serde::Serialize;

#[derive(Serialize, Clone, Debug)]
pub struct ObjectStats {
    pub alloc_counts: Counter,
    pub dealloc_counts: Counter,
}

#[derive(Serialize, Clone, Debug)]
pub struct SimStats {
    pub objects: ObjectStats,
    pub syscalls: Counter,
}

impl SimStats {
    pub fn new() -> Self {
        Self {
            objects: ObjectStats {
                alloc_counts: Counter::new(),
                dealloc_counts: Counter::new(),
            },
            syscalls: Counter::new(),
        }
    }
}
