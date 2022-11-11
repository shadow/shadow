use crate::utility::counter::Counter;

use anyhow::Context;
use serde::Serialize;

/// Simulation statistics.
#[derive(Clone, Debug)]
pub struct SimStats {
    pub alloc_counts: Counter,
    pub dealloc_counts: Counter,
    pub syscalls: Counter,
}

impl SimStats {
    pub fn new() -> Self {
        Self {
            alloc_counts: Counter::new(),
            dealloc_counts: Counter::new(),
            syscalls: Counter::new(),
        }
    }
}

/// Simulation statistics in the format to be output.
#[derive(Serialize, Clone, Debug)]
struct SimStatsForOutput {
    pub objects: ObjectStatsForOutput,
    pub syscalls: Counter,
}

#[derive(Serialize, Clone, Debug)]
struct ObjectStatsForOutput {
    pub alloc_counts: Counter,
    pub dealloc_counts: Counter,
}

impl SimStatsForOutput {
    pub fn new(stats: SimStats) -> Self {
        Self {
            objects: ObjectStatsForOutput {
                alloc_counts: stats.alloc_counts,
                dealloc_counts: stats.dealloc_counts,
            },
            syscalls: stats.syscalls,
        }
    }
}

pub fn write_stats_to_file(filename: &std::path::Path, stats: SimStats) -> anyhow::Result<()> {
    let stats = SimStatsForOutput::new(stats);

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
