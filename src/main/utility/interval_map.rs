// FIXME
#![allow(unused)]

#[derive(PartialEq, Eq, Debug)]
pub struct Interval {
    begin: usize,
    end: usize,
}

impl Interval {
    pub fn new(begin: usize, end: usize) -> Interval {
        assert!(begin <= end);
        Interval{begin, end}
    }

    pub fn begin(&self) -> usize {
        self.begin
    }

    pub fn end(&self) -> usize {
        self.end
    }
}

#[derive(PartialEq, Eq, Debug)]
pub enum Mutation<V> {
    ModifiedBegin(Interval, usize),
    ModifiedEnd(Interval, usize),
    Split(Interval, Interval, Interval),
    Removed(Interval, V),
}

pub struct IntervalMapIter<'a, V> {
    map : &'a IntervalMap<V>,
    i : usize,
}

impl<'a, V> Iterator for IntervalMapIter<'a, V> {
    type Item = (Interval, &'a V);

    fn next(&mut self) -> Option<Self::Item> {
        let i = self.i;
        let m = self.map;
        if i >= m.begins.len() {
            return None
        }
        let rv = Some((Interval::new(m.begins[i], m.ends[i]), &m.vals[i]));
        self.i += 1;
        rv
    }
}

pub struct IntervalMapCloneIter<'a, V> {
    it : IntervalMapIter<'a, V>,
}

impl<'a, V: Clone> Iterator for IntervalMapCloneIter<'a, V> {
    type Item = (Interval, V);

    fn next(&mut self) -> Option<Self::Item> {
        match self.it.next() {
            None => None,
            Some((interval, val)) => Some((interval, val.clone())),
        }
    }
}

pub struct IntervalMap<V> {
    begins: Vec<usize>,
    ends: Vec<usize>,
    vals: Vec<V>,
}

impl<V: Clone> IntervalMap<V> {
    pub fn new() -> IntervalMap<V> {
        IntervalMap {
            begins: Vec::new(),
            ends: Vec::new(),
            vals: Vec::new(),
        }
    }

    pub fn iter(&self) -> IntervalMapIter<V> {
        IntervalMapIter{map: self, i: 0}
    }

    pub fn iter_cloned(&self) -> IntervalMapCloneIter<V> {
        IntervalMapCloneIter{it: IntervalMapIter{map: self, i: 0}}
    }

    pub fn insert(&mut self, begin: usize, end: usize, val: V) -> Vec<Mutation<V>> {
        self.splice(begin, end, Some(val))
    }

    pub fn clear(&mut self, begin: usize, end: usize) -> Vec<Mutation<V>> {
        self.splice(begin, end, None)
    }

    // Splice zero or one value into the given interval
    fn splice(&mut self, begin: usize, end: usize, val: Option<V>) -> Vec<Mutation<V>> {
        assert!(begin <= end);
        let mut mutations = Vec::new();
        let mut begins_insertions = Vec::new();
        let mut ends_insertions = Vec::new();
        let mut vals_insertions = Vec::new();
        match val {
            Some(v) => {
                begins_insertions.push(begin);
                ends_insertions.push(end);
                vals_insertions.push(v);
            },
            None => (),
        };

        // We're eventually going to call Vec::splice on our vectors, and
        // this will be the starting index.
        let splice_start = match self.begins.binary_search(&begin) {
            Ok(i) | Err(i) => i,
        };

        println!("splice_start: {}", splice_start);

        // The eventual splice will be with a non-inclusive end-point. i.e. we start with
        // replacing no items, but will expand this if there are intervals we need to remove.
        let mut splice_end = splice_start;

        // Check whether there's an interval before the splice point,
        // and if so whether it overlaps.
        if splice_start > 0 && self.ends[splice_start-1] >= begin {
            let i = splice_start - 1;

            // If it ends after the end of our interval, we need to split it.
            if self.ends[i] > end {
                let old = Interval::new(self.begins[i], self.ends[i]);
                let new1 = Interval::new(self.begins[i], begin - 1);
                let new2 = Interval::new(end + 1, self.ends[i]);

                // Truncate the existing interval.
                self.ends[i] = new1.end();

                // Create a new interval, starting after the insertion interval.
                begins_insertions.push(new2.begin());
                ends_insertions.push(new2.end());
                vals_insertions.push(self.vals[i].clone());
                mutations.push(Mutation::Split(old, new1, new2));
            } else {
                // Otherwise we need to adjust its end to not overlap.
                let old = Interval::new(self.begins[i], self.ends[i]);
                self.ends[i] = begin - 1;
                mutations.push(Mutation::ModifiedEnd(old, self.ends[i]));
            }
        }

        // Find the end of the splice interval.
        // TODO: Maybe do binary search here.
        while splice_end < self.ends.len() && self.ends[splice_end] <= end {
            splice_end += 1
        }
        println!("splice_end: {}", splice_end);

        // Check whether we need to clip the beginning splice_end's interval.
        let mut modified_begin : Option<Mutation<V>> = None;
        if splice_end < self.begins.len() && self.begins[splice_end] <= end
                                          && self.ends[splice_end] > end {
            let i = splice_end;
            let old = Interval::new(self.begins[i], self.ends[i]);
            self.begins[i] = end + 1;
            modified_begin = Some(Mutation::ModifiedBegin(old, self.begins[i]));
        }

        // Do the splice
        let dropped_begins : Vec<_> = self.begins.splice(splice_start..splice_end, begins_insertions).collect();
        let dropped_ends : Vec<_> = self.ends.splice(splice_start..splice_end, ends_insertions).collect();
        {
            // We use the dropped_vals iterator directly here to avoid extra copies.
            // This is in a new scope to limit the lifetime of the mutable borrow from self.vals.
            let mut dropped_vals = self.vals.splice(splice_start..splice_end, vals_insertions);
            for i in 0..dropped_begins.len() {
                mutations.push(Mutation::Removed(
                        Interval::new(dropped_begins[i], dropped_ends[i]), dropped_vals.next().unwrap()));
            }
        }

        // Do the modified beginning, if any, last, so that mutations are ordered.
        match modified_begin {
            None => (),
            Some(m) => mutations.push(m),
        }

        mutations
    }

    fn item_at(&self, i: usize) -> (usize, usize, &V) {
        (self.begins[i], self.ends[i], &self.vals[i])
    }

    fn item_at_mut(&mut self, i: usize) -> (usize, usize, &mut V) {
        (self.begins[i], self.ends[i], &mut self.vals[i])
    }

    fn get_index(&self, x: usize) -> Option<usize> {
        match self.begins.binary_search(&x) {
            Ok(i) => Some(i),
            Err(i) => {
                if i == 0 {
                    None
                } else if self.ends[i-1] <= x {
                    Some(i)
                } else {
                    None
                }
            }
        }
    }

    pub fn get(&self, x: usize) -> Option<(usize, usize, &V)> {
        match self.get_index(x) {
            None => None,
            Some(i) => Some(self.item_at(i)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_insert_over_begin() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.insert(10, 20, "second".to_string()), [
            Mutation::ModifiedBegin(Interval::new(20, 30), 21),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(10, 20), "second".to_string()),
            (Interval::new(21, 30), "first".to_string()),
        ]);
    }

    #[test]
    fn test_insert_over_end() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.insert(30, 31, "second".to_string()), [
            Mutation::ModifiedEnd(Interval::new(20, 30), 29),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(20, 29), "first".to_string()),
            (Interval::new(30, 31), "second".to_string()),
        ]);
    }

    #[test]
    fn test_insert_removing() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.insert(10, 40, "second".to_string()), [
            Mutation::Removed(Interval::new(20, 30), "first".to_string()),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(10, 40), "second".to_string()),
        ]);
    }

    #[test]
    fn test_insert_forcing_split() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.insert(24, 25, "second".to_string()), [
            Mutation::Split(
                Interval::new(20, 30),
                Interval::new(20, 23),
                Interval::new(26, 30),
                ),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(20, 23), "first".to_string()),
            (Interval::new(24, 25), "second".to_string()),
            (Interval::new(26, 30), "first".to_string()),
        ]);
    }

    #[test]
    fn test_insert_all_mutations() {
        let mut m = IntervalMap::new();
        m.insert(0, 10, "first".to_string());
        m.insert(20, 30, "second".to_string());
        m.insert(40, 50, "third".to_string());
        assert_eq!(m.insert(10, 40, "clobbering".to_string()), [
            Mutation::ModifiedEnd(Interval::new(0, 10), 9),
            Mutation::Removed(Interval::new(20, 30), "second".to_string()),
            Mutation::ModifiedBegin(Interval::new(40, 50), 41),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(0, 9), "first".to_string()),
            (Interval::new(10, 40), "clobbering".to_string()),
            (Interval::new(41, 50), "third".to_string()),
        ]);
    }


    #[test]
    fn test_clear_over_begin() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.clear(10, 20), [
            Mutation::ModifiedBegin(Interval::new(20, 30), 21),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(21, 30), "first".to_string()),
        ]);
    }

    #[test]
    fn test_clear_over_end() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.clear(30, 31), [
            Mutation::ModifiedEnd(Interval::new(20, 30), 29),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(20, 29), "first".to_string()),
        ]);
    }

    #[test]
    fn test_clear_forcing_split() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.clear(24, 25), [
            Mutation::Split(
                Interval::new(20, 30),
                Interval::new(20, 23),
                Interval::new(26, 30),
                ),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
            (Interval::new(20, 23), "first".to_string()),
            (Interval::new(26, 30), "first".to_string()),
        ]);
    }

    #[test]
    fn test_clear_removing() {
        let mut m = IntervalMap::new();
        assert_eq!(m.insert(20, 30, "first".to_string()), []);
        assert_eq!(m.clear(10, 40), [
            Mutation::Removed(Interval::new(20, 30), "first".to_string()),
        ]);
        assert_eq!(m.iter_cloned().collect::<Vec<_>>(), [
        ]);
    }
}

/*
use std::collections::BTreeMap;
use std::ops::Bound::{Included, Unbounded};

pub struct IntervalMap<V> 
{
    begin_to_end_and_val: BTreeMap<usize, (usize, V)>,
}

pub struct ClippedEnd {
    begin : usize,
    old_end : usize,
}

pub struct ClippedBegin {
    old_begin : usize,
    new_begin : usize,
}

pub struct Split {
    begin: usize,
    old_end: usize,
    new_begin: usize,
    new_end: usize,
}

pub struct Item<V> {
    begin: usize,
    end: usize,
    val: V,
}

pub struct ClearResult<V>
{
    clipped_end : Option<ClippedEnd>,
    clipped_begin : Option<ClippedBegin>,
    split : Option<Split>,
    clobbered : Vec<Item<V>>,
}

impl<V: Clone> IntervalMap<V> {
    pub fn new() -> IntervalMap<V> {
        IntervalMap {begin_to_end_and_val: BTreeMap::new()}
    }

    fn last_interval_starting_before_or_on(&self, i: usize) -> Option<(usize, usize, &V)> {
        let mut before = self.begin_to_end_and_val.range((Unbounded, Included(i)));
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

    pub fn clear_range(&mut self, begin: usize, end: usize) -> ClearResult<V> {
        let mut clear_result = ClearResult{
            clipped_end: None,
            clipped_begin: None,
            split: None,
            clobbered : Vec::new()};

        for (b, (e, v)) in self.begin_to_end_and_val.range((Unbounded, Included(end))).rev() {
            if *e < begin {
                // Doesn't overlap
                break;
            }
            if *e > end {
                // Partial overlap over our end
                assert!(clear_result.clipped_begin.is_none());
                let new_begin = end+1;
                clear_result.clipped_begin = Some(
                    ClippedBegin{old_begin: *b, new_begin: new_begin});
            } else if b >= &begin {
                // Entirely contained in range
                clear_result.clobbered.push(Item{begin: *b, end: *e, val: v.clone()});
            } else {
                assert!(clear_result.clipped_end.is_none());
                clear_result.clipped_end = Some(
                    ClippedEnd{begin: *b, old_end: *e});
            }
        }

        // Remove intervals contained entirely in [begin, end]
        // TODO: If there are a lot of these it could be more efficient to split the tree using
        // `split_off` and paste the ends back together with `append`.
        for item in &clear_result.clobbered {
            self.begin_to_end_and_val.remove(&item.begin);
        }

        clear_result
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
*/




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
