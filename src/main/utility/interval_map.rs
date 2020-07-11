
/*
pub struct IntervalMap<V> {
    begins: Vec<usize>,
    ends: Vec<usize>,
    vals: Vec<V>,
}

impl<V> IntervalMap<V> {
    pub fn new() -> IntervalMap<V> {
        IntervalMap {
            begins: Vec::new(),
            ends: Vec::new(),
            vals: Vec::new(),
        }
    }

    fn idx_of_first_end_gte(&self, x: usize) -> Option<usize> {
        // TODO: We could do binary search here, but for relatively small maps linear search will
        // be faster anyway due to better cache performance. Might be interesting to implement a
        // "binary search until search-window is small, then linear search".
        for i in 0..self.ends.len() {
            if self.ends[i] >= x {
                return if self.begins[i] <= x {
                    Some(i)
                } else {
                    None
                }
            }
        }
        None
    }

    pub fn clear_range(&self, begin: usize, end: usize) {
    }

    pub fn get(&self, x: usize) -> Option<&V> {
        // TODO: We could do binary search here, but for relatively small maps linear search will
        // be faster anyway due to better cache performance. Might be interesting to implement a
        // "binary search until search-window is small, then linear search".
        for i in 0..self.begins.len() {
            if self.ends[i] >= x {
                return if self.begins[i] <= x {
                    Some(&self.vals[i])
                } else {
                    None
                }
            }
        }
        None
    }

    pub fn insert(&self, begin: usize, end: usize, val: V) {
        assert!(begin <= end);
    }
}
*/

use std::collections::BTreeMap;
use std::ops::Bound::Included;

pub struct IntervalMap<V> 
{
    begin_to_end_and_val: BTreeMap<usize, (usize, V)>,
}

impl<V> IntervalMap<V> {
    pub fn new() -> IntervalMap<V> {
        IntervalMap {begin_to_end_and_val: BTreeMap::new()}
    }

    fn last_interval_starting_before_or_on(&self, i: usize) -> Option<(usize, usize, &V)> {
        let mut before = self.begin_to_end_and_val.range((Included(0), Included(i)));
        match before.next_back() {
            None => None,
            Some ((last_begin, (last_end, v))) => Some((*last_begin, *last_end, v)),
        }
    }

    pub fn get(&self, i: usize) -> Option<&V> {
        match self.last_interval_starting_before_or_on(i) {
            None => None,
            Some((_, last_end, v)) => if last_end >= i { Some(v) } else { None },
        }
    }

    pub fn clear_range(&mut self, begin: usize, end: usize) {
        let mut begin_of_first_overlap = None;
        let mut begin_of_last_overlap = None;
        let mut begins_of_contained_ranges = Vec::new();

        for (b, (e, _)) in self.begin_to_end_and_val.range_mut((Included(0), Included(end))).rev() {
            if *e < begin {
                // Doesn't overlap
                break;
            }
            if *e > end {
                // Partial overlap over our end
                assert!(begin_of_last_overlap.is_none());
                begin_of_last_overlap = Some(*b);
            } else if b >= &begin {
                // Entirely contained in range
                begins_of_contained_ranges.push(*b);
            } else {
                assert!(begin_of_first_overlap.is_none());
                begin_of_first_overlap = Some(*b);
            }
        }

        // Adjust end of interval containing `begin`
        match begin_of_first_overlap {
            None => (),
            Some(b) => {
                let entry = self.begin_to_end_and_val.get_mut(&b).unwrap();
                //let old_end = entry.0;
                entry.0 = end - 1;
            }
        }

        // Adjust beginning of interval containing `end`
        match begin_of_last_overlap {
            None => (),
            Some(b) => { 
                // Take the entry out of the map
                let (e, v) = self.begin_to_end_and_val.remove(&b).unwrap();
                // Re-insert with a new beginning.
                self.begin_to_end_and_val.insert(end+1, (e, v));
            },
        }

        // Remove intervals contained entirely in [begin, end]
        // TODO: If there are a lot of these it could be more efficient to split the tree using
        // `split_off` and paste the ends back together with `append`.
        for b in begins_of_contained_ranges {
            self.begin_to_end_and_val.remove(&b);
        }
    }

    pub fn insert(&mut self, begin: usize, end: usize, v: V) {
        self.clear_range(begin, end);
        self.begin_to_end_and_val.insert(begin, (end, v));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_insert() {
        let mut m = IntervalMap::new();
        m.insert(21, 22, "21-22");

        // Start == end is ok
        m.insert(20, 20, "20-20");
    }

    #[test]
    #[should_panic]
    fn test_insert_overlap_start() {
        let mut m = IntervalMap::new();
        m.insert(20,30, "x");
        m.insert(10,20, "y");
    }

    #[test]
    #[should_panic]
    fn test_insert_overlap_end() {
        let mut m = IntervalMap::new();
        m.insert(20,30, "x");
        m.insert(30,40, "y");
    }

    #[test]
    #[should_panic]
    fn test_insert_in_middle() {
        let mut m = IntervalMap::new();
        m.insert(20,30, "x");
        m.insert(25,26, "y");
    }

    #[test]
    #[should_panic]
    fn test_insert_surrounding() {
        let mut m = IntervalMap::new();
        m.insert(20,30, "x");
        m.insert(10,40, "y");
    }

    #[test]
    fn test_get() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "20-30");
        m.insert(40, 50, "40-50");
        assert!(m.get(10).is_none());
        assert!(m.get(20).unwrap() == &"20-30");
        assert!(m.get(30).unwrap() == &"20-30");
        assert!(m.get(31).is_none());
        assert!(m.get(39).is_none());
        assert!(m.get(40).unwrap() == &"40-50");
        assert!(m.get(50).unwrap() == &"40-50");
        assert!(m.get(51).is_none());
    }
}




//type Point = usize;
//
//mod interval {
//    use super::Point;
//
//    #[derive(Copy, Clone)]
//    pub struct Interval {
//        pub begin: Point,
//        pub end: Point,
//        // Prevent direct construction
//        _private: (),
//    }
//
//    impl Interval {
//        pub fn new(begin: Point, end: Point) -> Interval {
//            assert!(begin <= end);
//            Interval{begin: begin, end: end, _private: ()}
//        }
//
//        pub fn is_before(&self, p: Point) -> bool {
//            self.end < p
//        }
//
//        pub fn is_after(&self, p: Point) -> bool {
//            self.begin > p
//        }
//
//        pub fn contains(&self, p: Point) -> bool {
//            !self.is_before(p) && !self.is_after(p)
//        }
//
//        /*
//        pub fn overlaps(&self, int: Interval) -> bool {
//            self.contains(int.begin) || int.contains(self.begin)
//        }
//        */
//    }
//
//    #[cfg(test)]
//    mod tests {
//        use super::*;
//
//        #[test]
//        #[should_panic]
//        fn test_invalid_interval() {
//            Interval::new(2,1);
//        }
//
//        #[test]
//        fn test_valid_intervals() {
//            // Just testing that we don't panic
//            Interval::new(1,1);
//            Interval::new(1,2);
//        }
//
//        #[test]
//        fn test_interval_before() {
//            let int = Interval::new(1,3);
//            assert!(!int.is_before(0));
//            assert!(!int.is_before(1));
//            assert!(!int.is_before(2));
//            assert!(!int.is_before(3));
//            assert!(int.is_before(4));
//        }
//
//        #[test]
//        fn test_interval_after() {
//            let int = Interval::new(1,3);
//            assert!(int.is_after(0));
//            assert!(!int.is_after(1));
//            assert!(!int.is_after(2));
//            assert!(!int.is_after(3));
//            assert!(!int.is_after(4));
//        }
//
//        #[test]
//        fn test_interval_contains() {
//            let int = Interval::new(1,3);
//            // Before
//            assert!(!int.contains(0));
//            // Begin edge
//            assert!(int.contains(1));
//            // Middle
//            assert!(int.contains(2));
//            // End edge
//            assert!(int.contains(3));
//            // After
//            assert!(!int.contains(4));
//        }
//
//        /*
//        #[test]
//        fn test_interval_overlaps() {
//            let int = Interval::new(10,20);
//            assert!(!int.overlaps(Interval::new(0,1)));
//        }
//        */
//    }
//}
//
//use interval::Interval;
//
//pub struct IntervalMapIter<'a, V> {
//    m: &'a IntervalMap<V>,
//    i: usize,
//}
//
//impl<'a, V> IntervalMapIter<'a, V> {
//    fn new(m: &'a IntervalMap<V>, i: usize) -> IntervalMapIter<'a, V> {
//        IntervalMapIter{m, i}
//    }
//
//    fn get(&self) -> Option<(Interval, &'a V)> {
//        match self.m.intervals.get(self.i) {
//            Some(int) => Some((*int, self.m.vals.get(self.i).unwrap())),
//            _ => None
//        }
//    }
//}
//
//impl<'a, V> Iterator for IntervalMapIter<'a, V> {
//    type Item = (Interval, &'a V);
//
//    fn next(&mut self) -> Option<Self::Item> {
//        let rv = self.get();
//        self.i += 1;
//        rv
//    }
//}
//
//pub struct IntervalMap<V> {
//    // We use parallel vectors here, rather than a single Vec<(Interval, V)>, for better cache
//    // performance while searching keys.
//    //
//    // The keys vector is kept in sorted order. We enforce that the intervals don't overlap.
//    //
//    // Insertion and removal could get slow for large maps, in which case we might consider some
//    // kind of tree, but for the current use-case of representing /proc/*/maps I expect that there
//    // won't be that many entries, and that they won't change that often. (Conversely going to a
//    // tree, in addition to adding complexity, would make lookups a bit slower due to additional
//    // pointer chasing).
//    intervals: Vec<Interval>,
//    vals: Vec<V>,
//}
//
//impl<V> IntervalMap<V> {
//    pub fn new() -> IntervalMap<V> {
//        IntervalMap {intervals: Vec::new(), vals: Vec::new()}
//    }
//
//    fn find_idx_of_last_interval_before(&self, pt: Point) -> Option<usize> {
//        // TODO: Implement binary search
//        for i in 0..self.intervals.len() {
//            if !self.intervals[i].is_before(pt) {
//                return match i {
//                    0 => None,
//                    _ => Some(i-1),
//                }
//            }
//        }
//        return None
//    }
//
//    pub fn find_last_interval_before(&self, pt: Point) -> Option<IntervalMapIter<V>> {
//        match self.find_idx_of_last_interval_before(pt) {
//            None => None,
//            Some(i) => Some(IntervalMapIter{m: self, i}),
//        }
//    }
//
//    pub fn insert(&mut self, int: Interval, v: V) {
//        match self.find_idx_of_last_interval_before(int.begin) {
//            None => {
//                assert!(self.intervals.is_empty() || !int.contains(self.intervals[0].begin));
//                self.intervals.insert(0, int);
//                self.vals.insert(0, v);
//            },
//            Some(i) => {
//                assert!((i+1) == self.intervals.len() || !int.contains(self.intervals[i+1].begin));
//                self.intervals.insert(i+1, int);
//                self.vals.insert(i+1, v);
//            },
//        }
//    }
//
//    pub fn iter(&self) -> IntervalMapIter<V> {
//        IntervalMapIter::new(self, 0)
//    }
//
//    pub fn find(&self, pt: Point) -> Option<IntervalMapIter<V>> {
//        // Get the index of the first interval *not* before pt.
//        let i =
//            match self.find_idx_of_last_interval_before(pt) {
//                None => 0,
//                Some(i) => i+1,
//            };
//        // Get the interval for that index, if any.
//        let int =
//            match self.intervals.get(i) {
//                None => return None,
//                Some(int) => int,
//            };
//        // Check whether the interval actually contains pt.
//        return if int.is_after(pt) {
//            None
//        } else {
//            Some(IntervalMapIter{m: self, i})
//        }
//    }
//}
//
//#[cfg(test)]
//mod tests {
//    use super::*;
//
//    #[test]
//    fn test_intervalmap() {
//        let mut m : IntervalMap<usize> = IntervalMap::new();
//        m.insert(Interval::new(0,0), 0);
//        let _it = m.find(0).unwrap();
//    }
//}
