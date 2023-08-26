/// A sequence number. We use a wrapper around a `u32` to prevent mistakes involving adding or
/// comparing sequence numbers.
#[derive(Copy, Clone, PartialEq, Eq)]
pub(crate) struct Seq(u32);

// We don't implement `From<u32>` or `Deref` since it makes it easier to accidentally mix up
// operating on a `u32` instead of a `Seq`. For example this can cause bugs if accidentally adding
// using `<u32 as Add<u32>>` instead of `<Seq as Add<u32>>`, which has a different wrapping
// behaviour. We don't implement `PartialOrd` or `Ord` since there is no ordering relation between
// arbitrary sequence numbers modulo 2^32.
static_assertions::assert_not_impl_any!(Seq: PartialOrd, Ord, From<u32>, std::ops::Deref);

impl Seq {
    #[inline]
    pub fn new(x: u32) -> Self {
        Self(x)
    }
}

impl std::fmt::Debug for Seq {
    #[inline]
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl std::fmt::Display for Seq {
    #[inline]
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl From<Seq> for u32 {
    #[inline]
    fn from(x: Seq) -> Self {
        x.0
    }
}

impl std::ops::Add<u32> for Seq {
    type Output = Self;

    fn add(self, offset: u32) -> Self::Output {
        Self::new(self.0.wrapping_add(offset))
    }
}

impl std::ops::Sub<u32> for Seq {
    type Output = Self;

    fn sub(self, offset: u32) -> Self::Output {
        Self::new(self.0.wrapping_sub(offset))
    }
}

impl std::ops::Sub for Seq {
    type Output = u32;

    fn sub(self, other: Self) -> Self::Output {
        self.0.wrapping_sub(other.0)
    }
}

impl std::ops::AddAssign<u32> for Seq {
    fn add_assign(&mut self, offset: u32) {
        self.0 = self.0.wrapping_add(offset);
    }
}

impl std::ops::SubAssign<u32> for Seq {
    fn sub_assign(&mut self, offset: u32) {
        self.0 = self.0.wrapping_sub(offset);
    }
}

/// A half-open range of sequence numbers modulo 2<sup>32</sup> bounded inclusively by `start` and
/// exclusively by `end`. The starting position can be greater than the ending position.
#[derive(Copy, Clone, PartialEq, Eq)]
pub(crate) struct SeqRange {
    /// Inclusive starting position.
    pub start: Seq,
    /// Exclusive ending position.
    pub end: Seq,
}

impl std::fmt::Debug for SeqRange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.start.fmt(f)?;
        write!(f, "..")?;
        self.end.fmt(f)?;
        Ok(())
    }
}

impl SeqRange {
    #[inline]
    pub fn new(start: Seq, end: Seq) -> Self {
        Self { start, end }
    }

    #[inline]
    pub fn len(&self) -> u32 {
        self.end - self.start
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns `true` if the sequence number is contained within this half-open range.
    #[inline]
    pub fn contains(&self, seq: Seq) -> bool {
        SeqRange::new(self.start, seq).len() < self.len()
    }

    /// Returns the intersecting range if there is a single intersecting range. Returns `None` if
    /// there is no intersection, or if there are two intersections. A range will always intersect
    /// with itself, which also holds for empty ranges. An empty range will intersect with a
    /// non-empty range if the empty range is contained within the non-empty range.
    pub fn intersection(&self, other: &Self) -> Option<SeqRange> {
        let a = self;
        let b = other;

        // handle empty ranges
        match (a.is_empty(), b.is_empty()) {
            // both ranges are empty, so they only intersect if they're equal
            (true, true) => return (a == b).then_some(*a),
            // A is empty, so they intersect if the start of A is contained within B
            (true, false) => return b.contains(a.start).then_some(*a),
            // B is empty, so they intersect if the start of B is contained within A
            (false, true) => return a.contains(b.start).then_some(*b),
            // neither range is empty, so continue to find a non-empty range
            (false, false) => {}
        }

        // check if edges of A are in B
        let a_0_in_b = b.contains(a.start);
        let a_1_in_b = b.contains(a.end - 1);

        match (a_0_in_b, a_1_in_b) {
            // intersection from left edge of A to right edge of B
            (true, false) => Some(Self::new(a.start, b.end)),
            // intersection from left edge of B to right edge of A
            (false, true) => Some(Self::new(b.start, a.end)),
            (true, true) => {
                if a.start - b.start < a.end - b.start {
                    // A wholly contained within B
                    Some(*a)
                } else {
                    // intersection from left edge of A to right edge of B, and intersection from
                    // left edge of B to right edge of A
                    None
                }
            }
            (false, false) => {
                // check if edges of B are in A
                let b_0_in_a = a.contains(b.start);
                let b_1_in_a = a.contains(b.end - 1);

                if b_0_in_a && b_1_in_a {
                    // B wholly contained within A
                    Some(*b)
                } else {
                    // no intersection
                    None
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // helper to make the tests fit on a single line
    fn range(start: u32, end: u32) -> SeqRange {
        SeqRange::new(Seq::new(start), Seq::new(end))
    }

    // helper to make the tests fit on a single line
    fn seq(val: u32) -> Seq {
        Seq::new(val)
    }

    #[test]
    fn test_range_contains() {
        // Test that `range` contains (or does not contain) `val`. Tests repeatedly with all offsets
        // in `offset_range` to make it easier to confirm that it also works across the 32-bit
        // wrapping point.
        fn test_range(
            range: SeqRange,
            val: Seq,
            contained: bool,
            offset_range: std::ops::Range<i32>,
        ) {
            for i in offset_range {
                // negative values will wrap to an equivalent positive offset
                let i = i as u32;

                let range = SeqRange::new(range.start + i, range.end + i);
                let val = val + i;

                assert_eq!(range.contains(val), contained);

                if !range.is_empty() {
                    let reverse_range = SeqRange::new(range.end, range.start);
                    assert_eq!(reverse_range.contains(val), !contained);
                }
            }
        }

        test_range(range(0, 0), seq(0), false, -10..10);
        test_range(range(0, 1), seq(0), true, -10..10);
        test_range(range(0, 1), seq(1), false, -10..10);
        test_range(range(0, 2), seq(0), true, -10..10);
        test_range(range(0, 2), seq(1), true, -10..10);
        test_range(range(0, 2), seq(2), false, -10..10);
    }

    #[test]
    fn test_range_intersection() {
        // Test that the intersection between `a` and `b` is equal to `expected`. Tests repeatedly
        // with all offsets in `offset_range` to make it easier to confirm that it also works across
        // the 32-bit wrapping point.
        fn test_pair(
            a: SeqRange,
            b: SeqRange,
            expected: impl Into<Option<SeqRange>>,
            offset_range: std::ops::Range<i32>,
        ) {
            let expected = expected.into();

            for i in offset_range {
                // negative values will wrap to an equivalent positive offset
                let i = i as u32;

                // add the offset to the ranges
                let a = SeqRange::new(a.start + i, a.end + i);
                let b = SeqRange::new(b.start + i, b.end + i);
                let expected = expected.map(|x| SeqRange::new(x.start + i, x.end + i));

                // make sure it's symmetric
                assert_eq!(a.intersection(&b), expected);
                assert_eq!(b.intersection(&a), expected);
            }
        }

        test_pair(range(0, 0), range(0, 0), range(0, 0), -10..10);
        test_pair(range(0, 0), range(1, 1), None, -10..10);
        test_pair(range(0, 0), range(0, 1), range(0, 0), -10..10);
        test_pair(range(1, 1), range(0, 1), None, -10..10);
        test_pair(range(0, 1), range(1, 2), None, -10..10);
        test_pair(range(0, 2), range(1, 2), range(1, 2), -10..10);
        test_pair(range(0, 2), range(0, 1), range(0, 1), -10..10);
        test_pair(range(10, 12), range(10, 12), range(10, 12), -100..100);
        test_pair(range(10, 12), range(12, 10), None, -100..100);

        // second test intersects twice (16-20 and 10-12), which returns a `None`
        test_pair(range(10, 20), range(12, 16), range(12, 16), -100..100);
        test_pair(range(10, 20), range(16, 12), None, -100..100);
    }
}
