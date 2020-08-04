/// Describes an inclusive interval [`begin`, `end`].
#[derive(PartialEq, Eq, Debug, Copy, Clone)]
pub struct Interval {
    begin: usize,
    end: usize,
}

impl Interval {
    /// Panics if `end` > `begin`.
    pub fn new(begin: usize, end: usize) -> Interval {
        assert!(begin <= end);
        Interval { begin, end }
    }

    pub fn begin(&self) -> usize {
        self.begin
    }

    pub fn end(&self) -> usize {
        self.end
    }
}

/// Describes modifications of an IntervalMap after overwriting an interval.
#[derive(PartialEq, Eq, Debug)]
pub enum Mutation<V> {
    ///       b     e
    /// from: |---v-|
    /// to:     |-v-|
    ///         b'
    /// Contains: ((b,e), b')
    ModifiedBegin(Interval, usize),
    ///       b     e
    /// from: |---v-|
    /// to:   |-v-|
    ///           e'
    /// Contains: ((b,e), e')
    ModifiedEnd(Interval, usize),
    ///        b             e
    /// from:  |-----v-------|
    /// to:    |-v--|  |--v--|
    ///        b   e'  b'    e
    ///
    /// Contains: ((b,e), (b,e'), (b',e)
    Split(Interval, Interval, Interval),
    ///       b     e
    /// from: |---v-|
    /// to:   
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
        if i >= m.begins.len() {
            return None;
        }
        let rv = Some((Interval::new(m.begins[i], m.ends[i]), &m.vals[i]));
        self.i += 1;
        rv
    }
}

pub struct KeyIter<'a, V> {
    map: &'a IntervalMap<V>,
    i: usize,
}

impl<'a, V> Iterator for KeyIter<'a, V> {
    type Item = Interval;

    fn next(&mut self) -> Option<Self::Item> {
        let i = self.i;
        let m = self.map;
        if i >= m.begins.len() {
            return None;
        }
        let rv = Some(Interval::new(m.begins[i], m.ends[i]));
        self.i += 1;
        rv
    }
}

#[derive(Clone, Debug)]
pub struct IntervalMap<V> {
    begins: Vec<usize>,
    ends: Vec<usize>,
    vals: Vec<V>,
}

/// Maps from non-overlapping `Interval`s to `V`.
impl<V: Clone> IntervalMap<V> {
    pub fn new() -> IntervalMap<V> {
        IntervalMap {
            begins: Vec::new(),
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

    /// Mutates the map so that the given range maps to nothing, modifying and removing intervals
    /// as needed. Returns what mutations were performed, including the values of any
    /// completely-removed intervals. If an interval is split (e.g. by inserting [5,6] into
    /// [0,10]), its value will be cloned.
    ///
    /// Returned mutations are ordered by their original interval values.
    pub fn clear(&mut self, begin: usize, end: usize) -> Vec<Mutation<V>> {
        self.splice(begin, end, None)
    }

    /// Insert range from `begin` to `end`, inclusive, mapping that range to `val`.  Existing
    /// contents of that range are cleared, and mutations returned, as for `clear`.
    pub fn insert(&mut self, begin: usize, end: usize, val: V) -> Vec<Mutation<V>> {
        self.splice(begin, end, Some(val))
    }

    // Splices zero or one value into the given interval.
    fn splice(&mut self, begin: usize, end: usize, val: Option<V>) -> Vec<Mutation<V>> {
        assert!(begin <= end);

        // List of mutations we had to perform to do the splice, which we'll ultimately return.
        let mut mutations = Vec::new();

        // Vectors that we'll ultimately splice insto self.{begins,ends,vals}.
        let mut begins_insertions = Vec::new();
        let mut ends_insertions = Vec::new();
        let mut vals_insertions = Vec::new();

        // We'll splice in the provided value, if any.
        if let Some(v) = val {
            begins_insertions.push(begin);
            ends_insertions.push(end);
            vals_insertions.push(v);
        };

        // We're eventually going to call Vec::splice on our vectors, and
        // this will be the starting index.
        let splice_start = match self.begins.binary_search(&begin) {
            Ok(i) | Err(i) => i,
        };

        // Check whether there's an interval before the splice point,
        // and if so whether it overlaps.
        if splice_start > 0 && self.ends[splice_start - 1] >= begin {
            let overlapping_idx = splice_start - 1;
            let overlapping_int =
                Interval::new(self.begins[overlapping_idx], self.ends[overlapping_idx]);

            // If it ends after the end of our interval, we need to split it.
            if overlapping_int.end() <= end {
                // overlapping_int :   -----
                // - (begin, end)  :      -----
                //           --->  :   ---
                self.ends[overlapping_idx] = begin - 1;
                mutations.push(Mutation::ModifiedEnd(
                    overlapping_int,
                    self.ends[overlapping_idx],
                ));
            } else {
                // overlapping_int : ----------
                // - (begin, end)  :    ----
                //           --->  : ---    ---
                let new1 = Interval::new(overlapping_int.begin(), begin - 1);
                let new2 = Interval::new(end + 1, overlapping_int.end());

                // Truncate the existing interval.
                self.ends[overlapping_idx] = new1.end();

                // Create a new interval, starting after the insertion interval.
                begins_insertions.push(new2.begin());
                ends_insertions.push(new2.end());
                vals_insertions.push(self.vals[overlapping_idx].clone());
                mutations.push(Mutation::Split(overlapping_int, new1, new2));
            }
        }

        // Find the end of the splice interval, which is the index of the first interval that ends
        // after the splice end (after having clipped the end of any existing interval contained in
        // the range, above).
        let splice_end = match self.ends.binary_search(&end) {
            Ok(i) if self.ends[i] <= end => i + 1,
            Ok(i) | Err(i) => i,
        };

        // Check whether we need to clip the beginning of splice_end's interval.
        let mut modified_begin: Option<Mutation<V>> = None;
        if splice_end < self.begins.len()
            && self.begins[splice_end] <= end
            && self.ends[splice_end] > end
        {
            let overlapping_idx = splice_end;
            let overlapping_int =
                Interval::new(self.begins[overlapping_idx], self.ends[overlapping_idx]);
            // overlapping_int :   ------
            // - (begin, end)  : -----
            //           --->  :      ---
            self.begins[overlapping_idx] = end + 1;
            // We don't push this onto `mutations` yet because we want to keep it sorted, and this
            // will always be the last mutation.
            modified_begin = Some(Mutation::ModifiedBegin(
                overlapping_int,
                self.begins[overlapping_idx],
            ));
        }

        // Do the splice into each parallel vector, tracking intervals that are dropped completely.
        // dropped         :   --- --- --- --- --- ----
        // - (begin, end)  : ----------------------------
        //           --->  :
        let dropped_begins = self
            .begins
            .splice(splice_start..splice_end, begins_insertions);
        let dropped_ends = self.ends.splice(splice_start..splice_end, ends_insertions);
        let mut dropped_vals = self.vals.splice(splice_start..splice_end, vals_insertions);
        for (dropped_begin, dropped_end) in dropped_begins.zip(dropped_ends) {
            mutations.push(Mutation::Removed(
                Interval::new(dropped_begin, dropped_end),
                dropped_vals.next().unwrap(),
            ));
        }

        // Do the modified beginning, if any, last, so that mutations are ordered.
        if let Some(m) = modified_begin {
            mutations.push(m)
        }

        mutations
    }

    // Returns the item at the given index.
    fn item_at(&self, i: usize) -> (Interval, &V) {
        (Interval::new(self.begins[i], self.ends[i]), &self.vals[i])
    }

    // Returns the index of the interval containing `x`.
    fn get_index(&self, x: usize) -> Option<usize> {
        match self.begins.binary_search(&x) {
            Ok(i) => Some(i),
            Err(i) => {
                if i == 0 {
                    None
                } else if x <= self.ends[i - 1] {
                    Some(i - 1)
                } else {
                    None
                }
            }
        }
    }

    // Returns the entry of the interval containing `x`.
    pub fn get(&self, x: usize) -> Option<(Interval, &V)> {
        match self.get_index(x) {
            None => None,
            Some(i) => Some(self.item_at(i)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn interval_sum<I>(i: I) -> usize
    where
        I: Iterator<Item = Interval>,
    {
        i.map(|x| x.end() - x.begin() + 1).sum()
    }

    fn validate_map<V: Clone>(m: &IntervalMap<V>) -> Result<(), String> {
        // Every interval is valid
        for i in m.keys() {
            if i.begin() > i.end() {
                return Err(format!("Invalid interval {:?}", i));
            }
        }

        // Intervals don't overlap
        for (i1, i2) in m.keys().zip(m.keys().skip(1)) {
            if i1.end() >= i2.begin() {
                return Err(format!("Overlapping intervals {:?} and {:?}", i1, i2));
            }
        }

        Ok(())
    }

    fn insert_and_sanity_check(
        m: &mut IntervalMap<String>,
        begin: usize,
        end: usize,
        val: &str,
    ) -> Result<Vec<Mutation<String>>, String> {
        let old_len_sum = interval_sum(m.keys());

        // Do the insert.
        let mutations = m.insert(begin, end, val.to_string());

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
        if !(new_len_sum >= (end - begin + 1)) {
            return Err(format!(
                "length-sum {} is smaller than inserted interval length {}",
                new_len_sum,
                end - begin + 1
            ));
        }
        if new_len == 0 {
            return Err(format!("new length is zero"));
        }

        Ok(mutations)
    }

    #[test]
    fn test_insert_random() {
        use rand::thread_rng;
        use rand::Rng;
        let dist = rand::distributions::Uniform::new_inclusive(0, 10);
        let mut rng = thread_rng();
        for i in 0..1000 {
            let mut m = IntervalMap::new();
            for j in 0..10 {
                let begin = rng.sample(dist);
                let end = begin + rng.sample(dist);
                let mut m_clone = m.clone();
                // Catch panics (failures) so that we can print the failing test case.
                let res = std::panic::catch_unwind(move || {
                    insert_and_sanity_check(&mut m_clone, begin, end, &format!("{}.{}", i, j))
                        .unwrap();
                    m_clone
                });
                if !res.is_ok() {
                    println!(
                        "Failed inserting {} -> {} into {:?}",
                        begin,
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
        begin: usize,
        end: usize,
        val: &str,
        expected_mutations: &[Mutation<String>],
        expected_val: &[(Interval, &str)],
    ) {
        let mutations = insert_and_sanity_check(m, begin, end, val).unwrap();

        // Validate the expected mutations.
        assert_eq!(mutations, expected_mutations);

        // Validate the expected new state.
        assert_eq!(
            m.iter().map(|(i, s)| (i, s.clone())).collect::<Vec<_>>(),
            expected_val
                .iter()
                .map(|(i, s)| (*i, s.to_string()))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn test_insert_into_empty() {
        let mut m = IntervalMap::new();
        insert_and_validate(&mut m, 10, 20, "x", &[], &[(Interval::new(10, 20), "x")]);
    }

    #[test]
    fn test_insert_after() {
        let mut m = IntervalMap::new();
        m.insert(1, 3, "i1".to_string());
        insert_and_validate(
            &mut m,
            4,
            6,
            "i2",
            &[],
            &[(Interval::new(1, 3), "i1"), (Interval::new(4, 6), "i2")],
        );
    }

    #[test]
    fn test_insert_before() {
        let mut m = IntervalMap::new();
        m.insert(4, 6, "i1".to_string());
        insert_and_validate(
            &mut m,
            1,
            3,
            "i2",
            &[],
            &[(Interval::new(1, 3), "i2"), (Interval::new(4, 6), "i1")],
        );
    }

    #[test]
    fn test_insert_over_begin() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        insert_and_validate(
            &mut m,
            10,
            20,
            "second",
            &[Mutation::ModifiedBegin(Interval::new(20, 30), 21)],
            &[
                (Interval::new(10, 20), "second"),
                (Interval::new(21, 30), "first"),
            ],
        );
    }

    #[test]
    fn test_insert_on_begin() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        insert_and_validate(
            &mut m,
            20,
            20,
            "second",
            &[Mutation::ModifiedBegin(Interval::new(20, 30), 21)],
            &[
                (Interval::new(20, 20), "second"),
                (Interval::new(21, 30), "first"),
            ],
        );
    }

    #[test]
    fn test_insert_over_end() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        insert_and_validate(
            &mut m,
            29,
            31,
            "second",
            &[Mutation::ModifiedEnd(Interval::new(20, 30), 28)],
            &[
                (Interval::new(20, 28), "first"),
                (Interval::new(29, 31), "second"),
            ],
        );
    }

    #[test]
    fn test_insert_on_end() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        insert_and_validate(
            &mut m,
            30,
            30,
            "second",
            &[Mutation::ModifiedEnd(Interval::new(20, 30), 29)],
            &[
                (Interval::new(20, 29), "first"),
                (Interval::new(30, 30), "second"),
            ],
        );
    }

    #[test]
    fn test_insert_removing() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        insert_and_validate(
            &mut m,
            10,
            40,
            "second",
            &[Mutation::Removed(
                Interval::new(20, 30),
                "first".to_string(),
            )],
            &[(Interval::new(10, 40), "second")],
        );
    }

    #[test]
    fn test_insert_forcing_split() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        insert_and_validate(
            &mut m,
            24,
            25,
            "second",
            &[Mutation::Split(
                Interval::new(20, 30),
                Interval::new(20, 23),
                Interval::new(26, 30),
            )],
            &[
                (Interval::new(20, 23), "first"),
                (Interval::new(24, 25), "second"),
                (Interval::new(26, 30), "first"),
            ],
        );
    }

    #[test]
    fn test_insert_over_exact() {
        let mut m = IntervalMap::new();
        m.insert(1, 1, "old".to_string());
        insert_and_validate(
            &mut m,
            1,
            1,
            "new",
            &[Mutation::Removed(
                Interval { begin: 1, end: 1 },
                "old".to_string(),
            )],
            &[(Interval::new(1, 1), "new")],
        );
    }

    #[test]
    fn test_insert_all_mutations() {
        let mut m = IntervalMap::new();
        m.insert(0, 10, "first".to_string());
        m.insert(20, 30, "second".to_string());
        m.insert(40, 50, "third".to_string());
        insert_and_validate(
            &mut m,
            10,
            40,
            "clobbering",
            &[
                Mutation::ModifiedEnd(Interval::new(0, 10), 9),
                Mutation::Removed(Interval::new(20, 30), "second".to_string()),
                Mutation::ModifiedBegin(Interval::new(40, 50), 41),
            ],
            &[
                (Interval::new(0, 9), "first"),
                (Interval::new(10, 40), "clobbering"),
                (Interval::new(41, 50), "third"),
            ],
        );
    }

    fn clear_and_sanity_check(
        m: &mut IntervalMap<String>,
        begin: usize,
        end: usize,
    ) -> Result<Vec<Mutation<String>>, String> {
        let old_len = interval_sum(m.keys());

        // Do the clear
        let mutations = m.clear(begin, end);

        // Validate general properties
        validate_map(m)?;

        let new_len = interval_sum(m.keys());
        if new_len > old_len {
            return Err(format!("new_len {} > old_len {}", new_len, old_len));
        }

        Ok(mutations)
    }

    #[test]
    fn test_clear_random() {
        use rand::thread_rng;
        use rand::Rng;
        let dist = rand::distributions::Uniform::new_inclusive(0, 10);
        let mut rng = thread_rng();
        for i in 0..1000 {
            let mut m = IntervalMap::new();
            for j in 0..10 {
                let insert_begin = rng.sample(dist);
                let insert_end = insert_begin + rng.sample(dist);
                let clear_begin = rng.sample(dist);
                let clear_end = clear_begin + rng.sample(dist);
                let mut m_clone = m.clone();
                // Catch panics (failures) so that we can print the failing test case.
                let res = std::panic::catch_unwind(move || {
                    clear_and_sanity_check(&mut m_clone, clear_begin, clear_end).unwrap();
                    insert_and_sanity_check(
                        &mut m_clone,
                        insert_begin,
                        insert_end,
                        &format!("{}.{}", i, j),
                    )
                    .unwrap();
                    m_clone
                });
                if !res.is_ok() {
                    println!(
                        "Failed after inserting {} -> {} and clearing {} -> {} in {:?}",
                        insert_begin,
                        insert_end,
                        clear_begin,
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
        begin: usize,
        end: usize,
        expected_mutations: &[Mutation<String>],
        expected_val: &[(Interval, &str)],
    ) {
        let mutations = clear_and_sanity_check(m, begin, end).unwrap();

        // Validate the expected mutations.
        assert_eq!(mutations, expected_mutations);

        // Validate the expected new state.
        assert_eq!(
            m.iter().map(|(i, s)| (i, s.clone())).collect::<Vec<_>>(),
            expected_val
                .iter()
                .map(|(i, s)| (*i, s.to_string()))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn test_clear_over_begin() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        clear_and_validate(
            &mut m,
            10,
            20,
            &[Mutation::ModifiedBegin(Interval::new(20, 30), 21)],
            &[(Interval::new(21, 30), "first")],
        );
    }

    #[test]
    fn test_clear_over_end() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        clear_and_validate(
            &mut m,
            30,
            31,
            &[Mutation::ModifiedEnd(Interval::new(20, 30), 29)],
            &[(Interval::new(20, 29), "first")],
        );
    }

    #[test]
    fn test_clear_forcing_split() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        clear_and_validate(
            &mut m,
            24,
            25,
            &[Mutation::Split(
                Interval::new(20, 30),
                Interval::new(20, 23),
                Interval::new(26, 30),
            )],
            &[
                (Interval::new(20, 23), "first"),
                (Interval::new(26, 30), "first"),
            ],
        );
    }

    #[test]
    fn test_clear_removing() {
        let mut m = IntervalMap::new();
        m.insert(20, 30, "first".to_string());
        clear_and_validate(
            &mut m,
            10,
            40,
            &[Mutation::Removed(
                Interval::new(20, 30),
                "first".to_string(),
            )],
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
        m.insert(1, 3, "interval".to_string());
        assert_eq!(m.get(0), None);
        assert_eq!(
            m.get(1),
            Some((Interval::new(1, 3), &"interval".to_string()))
        );
        assert_eq!(
            m.get(2),
            Some((Interval::new(1, 3), &"interval".to_string()))
        );
        assert_eq!(
            m.get(3),
            Some((Interval::new(1, 3), &"interval".to_string()))
        );
        assert_eq!(m.get(4), None);
    }

    #[test]
    fn test_get_two_intervals_with_gap() {
        let mut m = IntervalMap::<String>::new();
        m.insert(1, 3, "i1".to_string());
        m.insert(5, 7, "i2".to_string());
        assert_eq!(m.get(0), None);
        assert_eq!(m.get(1), Some((Interval::new(1, 3), &"i1".to_string())));
        assert_eq!(m.get(2), Some((Interval::new(1, 3), &"i1".to_string())));
        assert_eq!(m.get(3), Some((Interval::new(1, 3), &"i1".to_string())));
        assert_eq!(m.get(4), None);
        assert_eq!(m.get(5), Some((Interval::new(5, 7), &"i2".to_string())));
        assert_eq!(m.get(6), Some((Interval::new(5, 7), &"i2".to_string())));
        assert_eq!(m.get(7), Some((Interval::new(5, 7), &"i2".to_string())));
        assert_eq!(m.get(8), None);
    }

    #[test]
    fn test_get_two_intervals_without_gap() {
        let mut m = IntervalMap::<String>::new();
        m.insert(1, 3, "i1".to_string());
        m.insert(4, 6, "i2".to_string());
        assert_eq!(m.get(0), None);
        assert_eq!(m.get(1), Some((Interval::new(1, 3), &"i1".to_string())));
        assert_eq!(m.get(2), Some((Interval::new(1, 3), &"i1".to_string())));
        assert_eq!(m.get(3), Some((Interval::new(1, 3), &"i1".to_string())));
        assert_eq!(m.get(4), Some((Interval::new(4, 6), &"i2".to_string())));
        assert_eq!(m.get(5), Some((Interval::new(4, 6), &"i2".to_string())));
        assert_eq!(m.get(6), Some((Interval::new(4, 6), &"i2".to_string())));
        assert_eq!(m.get(7), None);
    }
}
