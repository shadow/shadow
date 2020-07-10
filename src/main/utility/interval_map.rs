type K = usize;
type S = usize;
type V = usize;

#[derive(Copy, Clone)]
pub struct Interval {
    base: K,
    len: S,
}

pub struct IntervalMapIter<'a> {
    m: &'a IntervalMap,
    i: usize,
}

impl<'a> IntervalMapIter<'a> {
    fn new(m: &'a IntervalMap, i: usize) -> IntervalMapIter<'a> {
        IntervalMapIter{m, i}
    }

    fn get(&self) -> Option<(Interval, &'a V)> {
        match self.m.keys.get(self.i) {
            Some(k) => Some((*k, self.m.vals.get(self.i).unwrap())),
            _ => None
        }
    }
}

impl<'a> Iterator for IntervalMapIter<'a> {
    type Item = (Interval, &'a V);

    fn next(&mut self) -> Option<Self::Item> {
        let rv = self.get();
        self.i += 1;
        rv
    }
}

pub struct IntervalMap {
    keys: Vec<Interval>,
    vals: Vec<V>,
}

impl IntervalMap {
    pub fn new() -> IntervalMap {
        IntervalMap {keys: Vec::new(), vals: Vec::new()}
    }

    pub fn insert(&mut self, k: Interval, v: V) {
        self.keys.push(k);
        self.vals.push(v);
    }

    pub fn to_iter(&self) -> IntervalMapIter {
        IntervalMapIter::new(self, 0)
    }

    /*
    pub fn find(&self, k: K) -> IntervalMapIter<'a> {
        for (b, s) in self.keys.iter() {
            if b <= &k && (b+s) > k {
                return
            }
        }
    }
    */

    /*
    pub fn delete(&mut self, k: K) {
    }
    */
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_intervalmap() {
        let _ = IntervalMap::new();
    }
}
