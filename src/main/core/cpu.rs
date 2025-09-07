pub struct RangeListIter<'a> {
    current_range: Option<std::ops::RangeInclusive<u32>>,
    remaining: &'a str,
}

impl Iterator for RangeListIter<'_> {
    type Item = u32;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            // if we have an active range to process
            if let Some(current_range) = &mut self.current_range {
                // get the next num in the range
                let Some(rv) = current_range.next() else {
                    // we've returned all numbers in the range, so clear the range and start over
                    self.current_range = None;
                    continue;
                };

                // return the next num in the range
                break Some(rv);
            // we don't have an active range, but there are more ranges to process
            } else if !self.remaining.is_empty() {
                let (next_range, remaining) = match self.remaining.split_once(',') {
                    // there was at least one comma
                    Some(x) => x,
                    // there are no more commas
                    None => (self.remaining, ""),
                };

                self.remaining = remaining;

                if next_range.is_empty() {
                    continue;
                }

                let mut split = next_range.split('-');
                let start = split.next().unwrap();
                let end = split.next();
                assert!(split.next().is_none());

                let start = start.parse().unwrap();
                let end = end.map(|x| x.parse().unwrap()).unwrap_or(start);

                self.current_range = Some(std::ops::RangeInclusive::new(start, end));

                continue;
            // no active range and no more ranges remaining
            } else {
                // the iterator is complete
                break None;
            }
        }
    }
}

/// Take an input of a list of ranges like '1-3,5,7-10' and return an iterator of integers like
/// \[1,2,3,5,7,8,9,10\].
///
/// The returned iterator will panic if the input is not nicely formatted (no whitespace, etc) or
/// contains invalid characters.
///
/// The iterator will return items in the order of the list, meaning that they are not guaranteed to
/// be returned in increasing order and there may be duplicates. For example "1,2,3,3,2" would
/// return items \[1, 2, 3, 3, 2\].
pub fn parse_range_list(range_list: &str) -> RangeListIter<'_> {
    RangeListIter {
        current_range: None,
        remaining: range_list,
    }
}

/// Get the nodes from `/sys/devices/system/node/possible`.
pub fn nodes() -> Vec<u32> {
    let name = "/sys/devices/system/node/possible";
    parse_range_list(std::fs::read_to_string(name).unwrap().trim()).collect()
}

/// Get the CPUs in a node from `/sys/devices/system/node/node{node}/cpulist`.
pub fn cpus(node: u32) -> Vec<u32> {
    let name = format!("/sys/devices/system/node/node{node}/cpulist");
    parse_range_list(std::fs::read_to_string(name).unwrap().trim()).collect()
}

/// Get the core ID from `/sys/devices/system/cpu/cpu{cpu}/topology/core_id`.
pub fn core(cpu: u32) -> u32 {
    let name = format!("/sys/devices/system/cpu/cpu{cpu}/topology/core_id");
    std::fs::read_to_string(name)
        .unwrap()
        .trim()
        .parse()
        .unwrap()
}

/// Get the online CPUs from `/sys/devices/system/cpu/online`.
pub fn online() -> Vec<u32> {
    let name = "/sys/devices/system/cpu/online";
    parse_range_list(std::fs::read_to_string(name).unwrap().trim()).collect()
}

/// Count the number of physical cores available. Uses `sched_getaffinity` so should take into
/// account CPU affinity and cgroups.
pub fn count_physical_cores() -> u32 {
    let affinity = nix::sched::sched_getaffinity(nix::unistd::Pid::from_raw(0)).unwrap();

    let mut physical_cores = std::collections::HashSet::new();

    for cpu in online() {
        if affinity.is_set(cpu.try_into().unwrap()).unwrap() {
            physical_cores.insert(core(cpu));
        }
    }

    assert!(!physical_cores.is_empty());
    physical_cores.len().try_into().unwrap()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn check(list: &str, array: &[u32]) {
        let list: Vec<_> = parse_range_list(list).collect();
        assert_eq!(list, array);
    }

    #[test]
    fn test_range_list() {
        check("", &[]);
        check("1", &[1]);
        check("1,2", &[1, 2]);
        check("1-2", &[1, 2]);
        check("1-1", &[1]);
        check("1,2,3", &[1, 2, 3]);
        check("1-3", &[1, 2, 3]);
        check("1,2-3,4", &[1, 2, 3, 4]);
        check("1,2-4,5", &[1, 2, 3, 4, 5]);
        check(
            "0-5,7-9,13,15-19",
            &[0, 1, 2, 3, 4, 5, 7, 8, 9, 13, 15, 16, 17, 18, 19],
        );
        check("1,,5", &[1, 5]);
        check("1,1,5", &[1, 1, 5]);
        check("1-1,5", &[1, 5]);
        check("1-1,0,5", &[1, 0, 5]);
        check("1-0", &[]);
    }
}
