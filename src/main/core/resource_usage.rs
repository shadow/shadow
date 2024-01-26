//! Utilities for getting system resource usage.

use std::fs::File;
use std::io::{Read, Seek};

use serde::Serialize;

/// Memory usage information parsed from '/proc/meminfo'. All units are converted to bytes.
#[derive(Copy, Clone, Debug, Default, Serialize)]
pub struct MemInfo {
    mem_total: Option<u64>,
    mem_free: Option<u64>,
    swap_total: Option<u64>,
    swap_free: Option<u64>,
    buffers: Option<u64>,
    cached: Option<u64>,
    s_reclaimable: Option<u64>,
    shmem: Option<u64>,
}

/// Collects some of the fields from '/proc/meminfo'. This function will seek to the start of the
/// file before reading.
pub fn meminfo(file: &mut File) -> std::io::Result<MemInfo> {
    let mut buffer = String::new();
    file.rewind()?;
    file.read_to_string(&mut buffer)?;

    let lines = buffer.lines().filter_map(|line| {
        let Some((name, val)) = line.split_once(':') else {
            // don't know how to parse this line
            return None;
        };

        let name = name.trim();
        let val = val.trim();

        let (val, unit) = val
            .rsplit_once(' ')
            .map(|(x, y)| (x, Some(y)))
            .unwrap_or((val, None));

        Some((name, val, unit))
    });

    let mut mem = MemInfo::default();

    for (name, val, unit) in lines {
        let Some(val) = val.parse().ok() else {
            // expected an integer
            continue;
        };

        match name {
            "MemTotal" => mem.mem_total = as_base_unit(val, unit),
            "MemFree" => mem.mem_free = as_base_unit(val, unit),
            "SwapTotal" => mem.swap_total = as_base_unit(val, unit),
            "SwapFree" => mem.swap_free = as_base_unit(val, unit),
            "Buffers" => mem.buffers = as_base_unit(val, unit),
            "Cached" => mem.cached = as_base_unit(val, unit),
            "SReclaimable" => mem.s_reclaimable = as_base_unit(val, unit),
            "Shmem" => mem.shmem = as_base_unit(val, unit),
            _ => {}
        }
    }

    Ok(mem)
}

/// Returns `None` if either the `unit` wasn't known, or the base unit is too large.
fn as_base_unit(val: u64, unit: Option<&str>) -> Option<u64> {
    let mul = match unit {
        None => 1,
        Some("B") => 1,
        Some("kB") => 1024,
        Some(_) => return None,
    };

    val.checked_mul(mul)
}
