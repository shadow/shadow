use std::ops::Range;

pub type Interval = Range<usize>;

/// Describes modifications of an IntervalMap after overwriting an interval.
#[derive(PartialEq, Eq, Debug)]
pub enum Mutation<V> {
    /// ```text
    ///       b     e
    /// from: |---v-|
    /// to:     |-v-|
    ///         b'
    /// ```
    ///
    /// Contains: ((b,e), b')
    ModifiedBegin(Interval, usize),
    /// ```text
    ///       b     e
    /// from: |---v-|
    /// to:   |-v-|
    ///           e'
    /// ```
    ///
    /// Contains: ((b,e), e')
    ModifiedEnd(Interval, usize),
    /// ```text
    ///        b             e
    /// from:  |-----v-------|
    /// to:    |-v--|  |--v--|
    ///        b   e'  b'    e
    /// ```
    ///
    /// Contains: ((b,e), (b,e'), (b',e)
    Split(Interval, Interval, Interval),
    /// ```text
    ///       b     e
    /// from: |---v-|
    /// to:
    /// ```
    ///
    /// Contains: (b,e), v
    Removed(Interval, V),
}

pub struct ItemIter<'a, V> {
    map: &'a IntervalMap<V>,
    i: usize,
}

impl<'a, V> Iterator for ItemIter<'a, V> {
    type Item = (Interval, &'a V);

    fn next(&mut self) -> Option<Self::Item> {
        let i = self.i;
        let m = self.map;
        if i >= m.starts.len() {
            return None;
        }
        let rv = Some(((m.starts[i]..m.ends[i]), &m.vals[i]));
        self.i += 1;
        rv
    }
}

pub struct KeyIter<'a, V> {
    map: &'a IntervalMap<V>,
    i: usize,
}

impl<V> Iterator for KeyIter<'_, V> {
    type Item = Interval;

    fn next(&mut self) -> Option<Self::Item> {
        let i = self.i;
        let m = self.map;
        if i >= m.starts.len() {
            return None;
        }
        let rv = Some(m.starts[i]..m.ends[i]);
        self.i += 1;
        rv
    }
}

#[derive(Clone, Debug)]
pub struct IntervalMap<V> {
    starts: Vec<usize>,
    ends: Vec<usize>,
    vals: Vec<V>,
}

/// Maps from non-overlapping `Interval`s to `V`.
impl<V: Clone> IntervalMap<V> {
    pub fn new() -> IntervalMap<V> {
        IntervalMap {
            starts: Vec::new(),
            ends: Vec::new(),
            vals: Vec::new(),
        }
    }

    /// Returns iterator over all intervals keys, in sorted order.
    pub fn keys(&self) -> KeyIter<V> {
        KeyIter { map: self, i: 0 }
    }

    /// Returns iterator over all intervals keys and their values, in order by interval key.
    pub fn iter(&self) -> ItemIter<V> {
        ItemIter { map: self, i: 0 }
    }

    /// Returns iterator over all interval keys and their values, starting with the first interval
    /// containing or after `begin`.
    pub fn iter_from(&self, begin: usize) -> ItemIter<V> {
        let idx = match self.starts.binary_search(&begin) {
            Ok(i) => i,
            Err(i) if (i > 0 && begin < self.ends[i - 1]) => i - 1,
            Err(i) => i,
        };
        ItemIter { map: self, i: idx }
    }

    /// Mutates the map so that the given range maps to nothing, modifying and removing intervals
    /// as needed. Returns what mutations were performed, including the values of any
    /// completely-removed intervals. If an interval is split (e.g. by inserting \[5,6\] into
    /// \[0,10\]), its value will be cloned.
    ///
    /// Returned mutations are ordered by their original interval values.
    pub fn clear(&mut self, interval: Interval) -> Vec<Mutation<V>> {
        self.splice(interval, None)
    }

    /// Insert range from `start` to `end`, inclusive, mapping that range to `val`.  Existing
    /// contents of that range are cleared, and mutations returned, as for `clear`.
    pub fn insert(&mut self, interval: Interval, val: V) -> Vec<Mutation<V>> {
        self.splice(interval, Some(val))
    }

    // Splices zero or one value into the given interval.
    fn splice(&mut self, interval: Interval, val: Option<V>) -> Vec<Mutation<V>> {
        let start = interval.start;
        let end = interval.end;

        // TODO: Support empty intervals?
        assert!(start < end);

        // List of mutations we had to perform to do the splice, which we'll ultimately return.
        let mut mutations = Vec::new();

        // Vectors that we'll ultimately splice insto self.{starts,ends,vals}.
        let mut starts_insertions = Vec::new();
        let mut ends_insertions = Vec::new();
        let mut vals_insertions = Vec::new();

        // We'll splice in the provided value, if any.
        if let Some(v) = val {
            starts_insertions.push(start);
            ends_insertions.push(end);
            vals_insertions.push(v);
        };

        // We're eventually going to call Vec::splice on our vectors, and
        // this will be the starting index.
        let splice_start = match self.starts.binary_search(&start) {
            Ok(i) | Err(i) => i,
        };

        // Check whether there's an interval before the splice point,
        // and if so whether it overlaps.
        if splice_start > 0 && self.ends[splice_start - 1] > start {
            let overlapping_idx = splice_start - 1;
            let overlapping_int = self.starts[overlapping_idx]..self.ends[overlapping_idx];

            if overlapping_int.end <= end {
                // overlapping_int :   -----
                // - (start, end)  :      -----
                //           --->  :   ---
                self.ends[overlapping_idx] = start;
                mutations.push(Mutation::ModifiedEnd(
                    overlapping_int,
                    self.ends[overlapping_idx],
                ));
            } else {
                // If it ends after the end of our interval, we need to split it.
                // overlapping_int : ----------
                // - (start, end)  :    ----
                //           --->  : ---    ---
                let new1 = overlapping_int.start..start;
                let new2 = end..overlapping_int.end;

                // Truncate the existing interval.
                self.ends[overlapping_idx] = new1.end;

                // Create a new interval, starting after the insertion interval.
                starts_insertions.push(new2.start);
                ends_insertions.push(new2.end);
                vals_insertions.push(self.vals[overlapping_idx].clone());
                mutations.push(Mutation::Split(overlapping_int, new1, new2));
            }
        }

        // Find the end of the splice interval, which is the index of the first interval that ends
        // after the splice end (after having clipped the end of any existing interval contained in
        // the range, above).
        let splice_end = match self.ends.binary_search(&end) {
            Ok(i) => i + 1,
            Err(i) => i,
        };

        // Check whether we need to clip the startning of splice_end's interval.
        let mut modified_start: Option<Mutation<V>> = None;
        if splice_end < self.starts.len()
            && self.starts[splice_end] < end
            && self.ends[splice_end] > end
        {
            let overlapping_idx = splice_end;
            let overlapping_int = self.starts[overlapping_idx]..self.ends[overlapping_idx];
            // overlapping_int :   ------
            // - (start, end)  : -----
            //           --->  :      ---
            self.starts[overlapping_idx] = end;
            // We don't push this onto `mutations` yet because we want to keep it sorted, and this
            // will always be the last mutation.
            modified_start = Some(Mutation::ModifiedBegin(
                overlapping_int,
                self.starts[overlapping_idx],
            ));
        }

        // Do the splice into each parallel vector, tracking intervals that are dropped completely.
        // dropped         :   --- --- --- --- --- ----
        // - (start, end)  : ----------------------------
        //           --->  :
        let dropped_starts = self
            .starts
            .splice(splice_start..splice_end, starts_insertions);
        let dropped_ends = self.ends.splice(splice_start..splice_end, ends_insertions);
        let mut dropped_vals = self.vals.splice(splice_start..splice_end, vals_insertions);
        for (dropped_start, dropped_end) in dropped_starts.zip(dropped_ends) {
            mutations.push(Mutation::Removed(
                dropped_start..dropped_end,
                dropped_vals.next().unwrap(),
            ));
        }

        // Do the modified startning, if any, last, so that mutations are ordered.
        if let Some(m) = modified_start {
            mutations.push(m)
        }

        mutations
    }

    // Returns the index of the interval containing `x`.
    fn get_index(&self, x: usize) -> Option<usize> {
        match self.starts.binary_search(&x) {
            Ok(i) => Some(i),
            Err(i) => {
                if i == 0 {
                    None
                } else if x < self.ends[i - 1] {
                    Some(i - 1)
                } else {
                    None
                }
            }
        }
    }

    // Returns the entry of the interval containing `x`.
    pub fn get(&self, x: usize) -> Option<(Interval, &V)> {
        self.get_index(x)
            .map(|i| (self.starts[i]..self.ends[i], &self.vals[i]))
    }

    // Returns the entry of the interval containing `x`.
    pub fn get_mut(&mut self, x: usize) -> Option<(Interval, &mut V)> {
        match self.get_index(x) {
            None => None,
            Some(i) => Some((self.starts[i]..self.ends[i], &mut self.vals[i])),
        }
    }
}

impl<V: Clone> Default for IntervalMap<V> {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn interval_sum<I>(i: I) -> usize
    where
        I: Iterator<Item = Interval>,
    {
        i.map(|x| x.end - x.start).sum()
    }

    fn validate_map<V: Clone>(m: &IntervalMap<V>) -> Result<(), String> {
        // Every interval is valid
        for i in m.keys() {
            // TODO: support empty interval keys?
            if i.start >= i.end {
                return Err(format!("Invalid interval {:?}", i));
            }
        }

        // Intervals don't overlap
        for (i1, i2) in m.keys().zip(m.keys().skip(1)) {
            if i1.end > i2.start {
                return Err(format!("Overlapping intervals {:?} and {:?}", i1, i2));
            }
        }

        Ok(())
    }

    fn insert_and_sanity_check(
        m: &mut IntervalMap<String>,
        interval: Interval,
        val: &str,
    ) -> Result<Vec<Mutation<String>>, String> {
        let old_len_sum = interval_sum(m.keys());

        // Do the insert.
        let mutations = m.insert(interval.clone(), val.to_string());

        // Validate general properties
        validate_map(m)?;

        let new_len_sum = interval_sum(m.keys());
        let new_len = m.keys().count();
        if new_len_sum < old_len_sum {
            return Err(format!(
                "length-sum shrunk from {} to {}",
                old_len_sum, new_len_sum
            ));
        }
        if new_len_sum < (interval.end - interval.start) {
            return Err(format!(
                "length-sum {} is smaller than inserted interval length {}",
                new_len_sum,
                interval.end - interval.start
            ));
        }
        if new_len == 0 {
            return Err("new length is zero".to_string());
        }

        Ok(mutations)
    }

    #[test]
    // Too slow for miri
    #[cfg_attr(miri, ignore)]
    fn test_insert_random() {
        use rand::Rng;
        use rand_core::SeedableRng;
        let dist = rand::distributions::Uniform::new_inclusive(0, 10);
        let mut rng = rand_chacha::ChaCha8Rng::seed_from_u64(10);
        for i in 0..1000 {
            let mut m = IntervalMap::new();
            for j in 0..10 {
                let start: usize = rng.sample(dist);
                let end: usize = start + rng.sample(dist) + 1;
                let mut m_clone = m.clone();
                // Catch panics (failures) so that we can print the failing test case.
                let res = std::panic::catch_unwind(move || {
                    insert_and_sanity_check(&mut m_clone, start..end, &format!("{}.{}", i, j))
                        .unwrap();
                    m_clone
                });
                if res.is_err() {
                    println!(
                        "Failed inserting {} -> {} into {:?}",
                        start,
                        end,
                        m.iter().map(|(i, s)| (i, s.clone())).collect::<Vec<_>>()
                    );
                }
                m = res.unwrap();
            }
        }
    }

    fn insert_and_validate(
        m: &mut IntervalMap<String>,
        interval: Interval,
        val: &str,
        expected_mutations: &[Mutation<String>],
        expected_val: &[(Interval, &str)],
    ) {
        let mutations = insert_and_sanity_check(m, interval, val).unwrap();

        // Validate the expected mutations.
        assert_eq!(mutations, expected_mutations);

        // Validate the expected new state.
        assert_eq!(
            m.iter().map(|(i, s)| (i, s.clone())).collect::<Vec<_>>(),
            expected_val
                .iter()
                .map(|(i, s)| (i.clone(), s.to_string()))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn test_insert_into_empty() {
        let mut m = IntervalMap::new();
        insert_and_validate(&mut m, 10..20, "x", &[], &[(10..20, "x")]);
    }

    #[test]
    fn test_insert_after() {
        let mut m = IntervalMap::new();
        m.insert(1..3, "i1".to_string());
        insert_and_validate(&mut m, 4..6, "i2", &[], &[(1..3, "i1"), (4..6, "i2")]);
    }

    #[test]
    fn test_insert_before() {
        let mut m = IntervalMap::new();
        m.insert(4..6, "i1".to_string());
        insert_and_validate(&mut m, 1..3, "i2", &[], &[(1..3, "i2"), (4..6, "i1")]);
    }

    #[test]
    fn test_insert_just_before() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            10..20,
            "second",
            &[],
            &[(10..20, "second"), (20..30, "first")],
        );
    }

    #[test]
    fn test_insert_over_start() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            10..21,
            "second",
            &[Mutation::ModifiedBegin(20..30, 21)],
            &[(10..21, "second"), (21..30, "first")],
        );
    }

    #[test]
    fn test_insert_on_start() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            20..21,
            "second",
            &[Mutation::ModifiedBegin(20..30, 21)],
            &[(20..21, "second"), (21..30, "first")],
        );
    }

    #[test]
    fn test_insert_just_after() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            30..40,
            "second",
            &[],
            &[(20..30, "first"), (30..40, "second")],
        );
    }

    #[test]
    fn test_insert_over_end() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            29..31,
            "second",
            &[Mutation::ModifiedEnd(20..30, 29)],
            &[(20..29, "first"), (29..31, "second")],
        );
    }

    #[test]
    fn test_insert_on_end() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            29..30,
            "second",
            &[Mutation::ModifiedEnd(20..30, 29)],
            &[(20..29, "first"), (29..30, "second")],
        );
    }

    #[test]
    fn test_insert_removing() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            10..40,
            "second",
            &[Mutation::Removed(20..30, "first".to_string())],
            &[(10..40, "second")],
        );
    }

    #[test]
    fn test_insert_forcing_split() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        insert_and_validate(
            &mut m,
            24..25,
            "second",
            &[Mutation::Split(20..30, 20..24, 25..30)],
            &[(20..24, "first"), (24..25, "second"), (25..30, "first")],
        );
    }

    #[test]
    fn test_insert_over_exact() {
        let mut m = IntervalMap::new();
        m.insert(1..2, "old".to_string());
        insert_and_validate(
            &mut m,
            1..2,
            "new",
            &[Mutation::Removed(1..2, "old".to_string())],
            &[(1..2, "new")],
        );
    }

    #[test]
    fn test_insert_all_mutations() {
        let mut m = IntervalMap::new();
        m.insert(0..10, "first".to_string());
        m.insert(20..30, "second".to_string());
        m.insert(40..50, "third".to_string());
        insert_and_validate(
            &mut m,
            5..45,
            "clobbering",
            &[
                Mutation::ModifiedEnd(0..10, 5),
                Mutation::Removed(20..30, "second".to_string()),
                Mutation::ModifiedBegin(40..50, 45),
            ],
            &[(0..5, "first"), (5..45, "clobbering"), (45..50, "third")],
        );
    }

    fn clear_and_sanity_check(
        m: &mut IntervalMap<String>,
        start: usize,
        end: usize,
    ) -> Result<Vec<Mutation<String>>, String> {
        let old_len = interval_sum(m.keys());

        // Do the clear
        let mutations = m.clear(start..end);

        // Validate general properties
        validate_map(m)?;

        let new_len = interval_sum(m.keys());
        if new_len > old_len {
            return Err(format!("new_len {} > old_len {}", new_len, old_len));
        }

        Ok(mutations)
    }

    #[test]
    // Too slow for miri
    #[cfg_attr(miri, ignore)]
    fn test_clear_random() {
        use rand::Rng;
        use rand_core::SeedableRng;
        let dist = rand::distributions::Uniform::new_inclusive(0, 10);
        let mut rng = rand_chacha::ChaCha8Rng::seed_from_u64(10);
        for i in 0..1000 {
            let mut m = IntervalMap::new();
            for j in 0..10 {
                let insert_start = rng.sample(dist);
                let insert_end = insert_start + 1 + rng.sample(dist);
                let clear_start = rng.sample(dist);
                let clear_end = clear_start + 1 + rng.sample(dist);
                let mut m_clone = m.clone();
                // Catch panics (failures) so that we can print the failing test case.
                let res = std::panic::catch_unwind(move || {
                    clear_and_sanity_check(&mut m_clone, clear_start, clear_end).unwrap();
                    insert_and_sanity_check(
                        &mut m_clone,
                        insert_start..insert_end,
                        &format!("{}.{}", i, j),
                    )
                    .unwrap();
                    m_clone
                });
                if res.is_err() {
                    println!(
                        "Failed after inserting {} -> {} and clearing {} -> {} in {:?}",
                        insert_start,
                        insert_end,
                        clear_start,
                        clear_end,
                        m.iter().map(|(i, s)| (i, s.clone())).collect::<Vec<_>>()
                    );
                }
                m = res.unwrap();
            }
        }
    }

    fn clear_and_validate(
        m: &mut IntervalMap<String>,
        interval: Interval,
        expected_mutations: &[Mutation<String>],
        expected_val: &[(Interval, &str)],
    ) {
        let start = interval.start;
        let end = interval.end;
        let mutations = clear_and_sanity_check(m, start, end).unwrap();

        // Validate the expected mutations.
        assert_eq!(mutations, expected_mutations);

        // Validate the expected new state.
        assert_eq!(
            m.iter().map(|(i, s)| (i, s.clone())).collect::<Vec<_>>(),
            expected_val
                .iter()
                .map(|(i, s)| (i.clone(), s.to_string()))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn test_clear_over_start() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        clear_and_validate(
            &mut m,
            10..21,
            &[Mutation::ModifiedBegin(20..30, 21)],
            &[(21..30, "first")],
        );
    }

    #[test]
    fn test_clear_over_end() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        clear_and_validate(
            &mut m,
            29..31,
            &[Mutation::ModifiedEnd(20..30, 29)],
            &[(20..29, "first")],
        );
    }

    #[test]
    fn test_clear_forcing_split() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        clear_and_validate(
            &mut m,
            24..25,
            &[Mutation::Split(20..30, 20..24, 25..30)],
            &[(20..24, "first"), (25..30, "first")],
        );
    }

    #[test]
    fn test_clear_removing() {
        let mut m = IntervalMap::new();
        m.insert(20..30, "first".to_string());
        clear_and_validate(
            &mut m,
            10..40,
            &[Mutation::Removed(20..30, "first".to_string())],
            &[],
        );
    }

    #[test]
    fn test_get_empty() {
        let m = IntervalMap::<String>::new();
        assert_eq!(m.get(10), None);
    }

    #[test]
    fn test_get_single_interval() {
        let mut m = IntervalMap::<String>::new();
        m.insert(1..4, "interval".to_string());
        assert_eq!(m.get(0), None);
        assert_eq!(m.get(1), Some((1..4, &"interval".to_string())));
        assert_eq!(m.get(2), Some((1..4, &"interval".to_string())));
        assert_eq!(m.get(3), Some((1..4, &"interval".to_string())));
        assert_eq!(m.get(4), None);
        assert_eq!(m.get(5), None);
    }

    #[test]
    fn test_get_two_intervals_with_gap() {
        let mut m = IntervalMap::<String>::new();
        m.insert(1..4, "i1".to_string());
        m.insert(5..9, "i2".to_string());
        assert_eq!(m.get(0), None);
        assert_eq!(m.get(1), Some((1..4, &"i1".to_string())));
        assert_eq!(m.get(2), Some((1..4, &"i1".to_string())));
        assert_eq!(m.get(3), Some((1..4, &"i1".to_string())));
        assert_eq!(m.get(4), None);
        assert_eq!(m.get(5), Some((5..9, &"i2".to_string())));
        assert_eq!(m.get(6), Some((5..9, &"i2".to_string())));
        assert_eq!(m.get(7), Some((5..9, &"i2".to_string())));
        assert_eq!(m.get(8), Some((5..9, &"i2".to_string())));
        assert_eq!(m.get(9), None);
    }

    #[test]
    fn test_get_two_intervals_without_gap() {
        let mut m = IntervalMap::<String>::new();
        m.insert(1..3, "i1".to_string());
        m.insert(3..6, "i2".to_string());
        assert_eq!(m.get(0), None);
        assert_eq!(m.get(1), Some((1..3, &"i1".to_string())));
        assert_eq!(m.get(2), Some((1..3, &"i1".to_string())));
        assert_eq!(m.get(3), Some((3..6, &"i2".to_string())));
        assert_eq!(m.get(4), Some((3..6, &"i2".to_string())));
        assert_eq!(m.get(5), Some((3..6, &"i2".to_string())));
        assert_eq!(m.get(6), None);
    }

    #[test]
    fn test_iter_from() {
        let mut m = IntervalMap::<&str>::new();
        m.insert(1..3, "i1");
        m.insert(4..6, "i2");
        assert_eq!(
            m.iter_from(0).collect::<Vec<_>>(),
            vec![(1..3, &"i1"), (4..6, &"i2")]
        );
        assert_eq!(
            m.iter_from(1).collect::<Vec<_>>(),
            vec![(1..3, &"i1"), (4..6, &"i2")]
        );
        assert_eq!(
            m.iter_from(2).collect::<Vec<_>>(),
            vec![(1..3, &"i1"), (4..6, &"i2")]
        );
        assert_eq!(m.iter_from(3).collect::<Vec<_>>(), vec![(4..6, &"i2")]);
        assert_eq!(m.iter_from(4).collect::<Vec<_>>(), vec![(4..6, &"i2")]);
        assert_eq!(m.iter_from(5).collect::<Vec<_>>(), vec![(4..6, &"i2")]);
        assert_eq!(m.iter_from(6).collect::<Vec<_>>(), vec![]);
        assert_eq!(m.iter_from(7).collect::<Vec<_>>(), vec![]);
    }
}
