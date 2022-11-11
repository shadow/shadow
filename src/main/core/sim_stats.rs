use crate::utility::counter::Counter;

use anyhow::Context;
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

pub fn write_stats_to_file(filename: &std::path::Path, stats: SimStats) -> anyhow::Result<()> {
    let file = std::fs::File::create(&filename)
        .with_context(|| format!("Failed to create file '{}'", filename.display()))?;

    serde_json::to_writer_pretty(file, &stats).with_context(|| {
        format!(
            "Failed to write stats json to file '{}'",
            filename.display()
        )
    })?;

    Ok(())
}
