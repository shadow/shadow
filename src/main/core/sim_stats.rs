use std::cell::RefCell;
use std::sync::Mutex;

use anyhow::Context;
use serde::Serialize;

use crate::utility::counter::Counter;

/// Simulation statistics to be accessed by a single thread.
#[derive(Debug)]
pub struct LocalSimStats {
    pub alloc_counts: RefCell<Counter>,
    pub dealloc_counts: RefCell<Counter>,
    pub syscall_counts: RefCell<Counter>,
}

impl LocalSimStats {
    pub fn new() -> Self {
        Self {
            alloc_counts: RefCell::new(Counter::new()),
            dealloc_counts: RefCell::new(Counter::new()),
            syscall_counts: RefCell::new(Counter::new()),
        }
    }
}

impl Default for LocalSimStats {
    fn default() -> Self {
        Self::new()
    }
}

/// Simulation statistics to be accessed by multiple threads.
#[derive(Debug)]
pub struct SharedSimStats {
    pub alloc_counts: Mutex<Counter>,
    pub dealloc_counts: Mutex<Counter>,
    pub syscall_counts: Mutex<Counter>,
}

impl SharedSimStats {
    pub fn new() -> Self {
        Self {
            alloc_counts: Mutex::new(Counter::new()),
            dealloc_counts: Mutex::new(Counter::new()),
            syscall_counts: Mutex::new(Counter::new()),
        }
    }

    /// Add stats from a local object to a shared object. May reset fields of `local`.
    pub fn add_from_local_stats(&self, local: &LocalSimStats) {
        let mut shared_alloc_counts = self.alloc_counts.lock().unwrap();
        let mut shared_dealloc_counts = self.dealloc_counts.lock().unwrap();
        let mut shared_syscall_counts = self.syscall_counts.lock().unwrap();

        let mut local_alloc_counts = local.alloc_counts.borrow_mut();
        let mut local_dealloc_counts = local.dealloc_counts.borrow_mut();
        let mut local_syscall_counts = local.syscall_counts.borrow_mut();

        shared_alloc_counts.add_counter(&local_alloc_counts);
        shared_dealloc_counts.add_counter(&local_dealloc_counts);
        shared_syscall_counts.add_counter(&local_syscall_counts);

        *local_alloc_counts = Counter::new();
        *local_dealloc_counts = Counter::new();
        *local_syscall_counts = Counter::new();
    }
}

impl Default for SharedSimStats {
    fn default() -> Self {
        Self::new()
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
    /// Takes data from `stats` and puts it into a structure designed for output. May reset fields
    /// of `stats`.
    pub fn new(stats: &SharedSimStats) -> Self {
        Self {
            objects: ObjectStatsForOutput {
                alloc_counts: std::mem::take(&mut stats.alloc_counts.lock().unwrap()),
                dealloc_counts: std::mem::take(&mut stats.dealloc_counts.lock().unwrap()),
            },
            syscalls: std::mem::take(&mut stats.syscall_counts.lock().unwrap()),
        }
    }
}

/// May reset fields of `stats`.
pub fn write_stats_to_file(
    filename: &std::path::Path,
    stats: &SharedSimStats,
) -> anyhow::Result<()> {
    let stats = SimStatsForOutput::new(stats);

    let file = std::fs::File::create(filename)
        .with_context(|| format!("Failed to create file '{}'", filename.display()))?;

    serde_json::to_writer_pretty(file, &stats).with_context(|| {
        format!(
            "Failed to write stats json to file '{}'",
            filename.display()
        )
    })?;

    Ok(())
}
