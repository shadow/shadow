use std::cmp::Ordering;
use std::collections::{BinaryHeap, HashSet, VecDeque};
use std::fmt::Debug;
use std::hash::Hash;

/// Tracks the order in which items are pushed into the queue, which is useful for some disciplines.
type PushOrder = u64;

/// An item wrapper that allows us to implement a min-heap WRT item priority. Our implementation of
/// the `Ord` trait is such that the item containing the smallest priority (across all items) will
/// be dequeued first, where ties are broken by preferring the item that was pushed into the queue
/// first. This wrapper guarantees that item priority does not change while the item is queued; to
/// change an item's priority, it must be dequeued and then re-enqueued.
struct Prioritized<T> {
    item: T,
    priority: u64,
    push_order: PushOrder,
}

impl<T> Ord for Prioritized<T> {
    fn cmp(&self, other: &Self) -> Ordering {
        // This ordering is intended to implement a min-heap based on item priority, where smaller
        // values have precedence over larger values. When items are equal, we break ties using the
        // order in which items were pushed into the queue (which should always be unique in our
        // implementation).
        let ordering = match self.priority.cmp(&other.priority) {
            Ordering::Less => Ordering::Greater,
            Ordering::Greater => Ordering::Less,
            // `reverse()` so that items pushed first (with a smaller `push_order`) are preferred.
            Ordering::Equal => self.push_order.cmp(&other.push_order).reverse(),
        };
        assert_ne!(ordering, Ordering::Equal);
        ordering
    }
}

impl<T> PartialOrd for Prioritized<T> {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        // We want PartialOrd to be consistent with Ord.
        Some(self.cmp(other))
    }
}

impl<T> Eq for Prioritized<T> {}

impl<T> PartialEq for Prioritized<T> {
    fn eq(&self, other: &Self) -> bool {
        // PartialEq must be consistent with PartialOrd. The `Prioritized` object is designed to
        // never be equal so that its ordering is never ambiguous, preventing non-determinism.
        let is_equal = self.partial_cmp(other) == Some(Ordering::Equal);
        assert!(!is_equal);
        is_equal
    }
}

/// The kinds of queuing disciplines the `NetworkQueue` currently supports.
pub enum NetworkQueueKind {
    /// A queue where items are sorted and dequeued based on a priority given at enqueue time.
    /// Queues created with this discipline MUST provide `Some` priorities to
    /// `NetworkQueue::push()`.
    MinPriority,
    /// A queue where items are sorted and dequeued based on the order in which they are enqueued.
    /// Queues created with this discipline MAY provide `None` priorities to `NetworkQueue::push()`.
    FirstInFirstOut,
}

/// The queuing discipline to use to define the order in which enqueued items are dequeued. We use
/// different data structures to help us realize different queuing strategies. The supported
/// variants match those described in `NetworkQueueKind`.
///
/// Note that the `NetworkQueue` will live for the lifetime of the simulation and these qdisc data
/// structures will only ever grow and not shrink. Thus, if there's a burst of packets, the queue
/// will grow to accommodate them but will never shrink again to reclaim memory.
enum QueuingDiscipline<T> {
    /// See `NetworkQueueKind::MinPriority`.
    MinPriority((BinaryHeap<Prioritized<T>>, PushOrder)),
    /// See `NetworkQueueKind::FirstInFirstOut`.
    FirstInFirstOut(VecDeque<T>),
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum PushError {
    /// Indicates that the item being added to the queue is already in the queue.
    AlreadyQueued,
    /// A priority value was not provided when one is required.
    NonePriority,
}

/// A queue that can hold a group of items (e.g., sockets or packets) and that provides some control
/// over the queuing discipline and behavior. The queue stores two copies of every enqueued item:
/// the items and their clones must be such that they are considered equal. Thus, it may be useful
/// to wrap items with a `std::rc::Rc` or `std::sync::Arc` before enqueuing.
///
/// The queue implementation requires that inserted items are not modified in such a way that an
/// item's hash, as determined by the `Hash` trait, or its equality, as determined by the `Eq`
/// trait, changes while it is in the queue. These requirements are consistent with those of a
/// [`HashSet`](https://doc.rust-lang.org/std/collections/struct.HashSet.html).
pub struct NetworkQueue<T: Clone + Debug + Eq + Hash> {
    /// The set of items that currently exist in the queue.
    membership: HashSet<T>,
    /// A cloned set of items organized according to the configured queuing discipline.
    queue: QueuingDiscipline<T>,
}

impl<T: Clone + Debug + Eq + Hash> NetworkQueue<T> {
    /// Create a new queue that will organize items according to the given `NetworkQueueKind`.
    pub fn new(kind: NetworkQueueKind) -> Self {
        let queue = match kind {
            NetworkQueueKind::MinPriority => QueuingDiscipline::MinPriority((BinaryHeap::new(), 0)),
            NetworkQueueKind::FirstInFirstOut => {
                QueuingDiscipline::FirstInFirstOut(VecDeque::new())
            }
        };
        Self {
            membership: HashSet::new(),
            queue,
        }
    }

    /// Remove and drop all items from the queue.
    pub fn clear(&mut self) {
        match &mut self.queue {
            QueuingDiscipline::MinPriority((heap, _)) => heap.clear(),
            QueuingDiscipline::FirstInFirstOut(deque) => deque.clear(),
        }
        self.membership.clear();
    }

    /// Returns true if the queue contains the item, and false otherwise.
    pub fn contains(&self, item: &T) -> bool {
        self.membership.contains(item)
    }

    /// Returns true if the queue does not contain any items.
    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the number of queued items.
    pub fn len(&self) -> usize {
        self.membership.len()
    }

    /// Returns the next available item if one exists. This function also drops the duplicate cloned
    /// handle that was created in `NetworkQueue::try_push()`.
    pub fn pop(&mut self) -> Option<T> {
        match &mut self.queue {
            QueuingDiscipline::MinPriority((heap, _)) => heap.pop().map(|x| x.item),
            QueuingDiscipline::FirstInFirstOut(deque) => deque.pop_front(),
        }
        .inspect(|x| {
            assert!(self.membership.remove(x));
        })
    }

    /// Pushes an item into the queue, internally cloning it to support membership checks. It may be
    /// useful to wrap items with a `std::rc::Rc` or `std::sync::Arc` before calling this function.
    ///
    /// The item should not be modified while in the queue such that it's hash or equality changes.
    ///
    /// # Panics
    ///
    /// This function panics if the queue was configured with `NetworkQueueKind::MinPriority` and
    /// `maybe_priority` is `None` or if the item already exists in the queue.
    pub fn push(&mut self, item: T, maybe_priority: Option<u64>) {
        self.try_push(item, maybe_priority).unwrap()
    }

    /// Tries to push an item into the queue. If successful, the item is internally cloned to
    /// support membership checks, so it may be useful to wrap items with a `std::rc::Rc` or
    /// `std::sync::Arc` before calling this function. If unsuccessful, a `PushError` is returned
    /// encoding the reason for the failure.
    ///
    /// The item should not be modified while in the queue such that it's hash or equality changes.
    pub fn try_push(&mut self, item: T, maybe_priority: Option<u64>) -> Result<(), PushError> {
        if self.contains(&item) {
            Err(PushError::AlreadyQueued)
        } else {
            assert!(self.membership.insert(item.clone()));
            match &mut self.queue {
                QueuingDiscipline::MinPriority((heap, counter)) => {
                    if let Some(priority) = maybe_priority {
                        let push_order = *counter;
                        *counter += 1;
                        heap.push(Prioritized {
                            item,
                            priority,
                            push_order,
                        })
                    } else {
                        return Err(PushError::NonePriority);
                    }
                }
                QueuingDiscipline::FirstInFirstOut(deque) => deque.push_back(item),
            };
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use std::rc::Rc;

    use super::*;

    fn len_helper<T: Clone + Debug + Eq + Hash>(q: &NetworkQueue<T>, len: usize) {
        match &q.queue {
            QueuingDiscipline::MinPriority((heap, _)) => assert_eq!(heap.len(), len),
            QueuingDiscipline::FirstInFirstOut(deque) => assert_eq!(deque.len(), len),
        };
        assert_eq!(q.membership.len(), len);
        assert_eq!(q.len(), len);
        if len == 0 {
            assert!(q.is_empty());
        } else {
            assert!(!q.is_empty());
        }
    }

    fn new_helper(kind: NetworkQueueKind) -> NetworkQueue<String> {
        let q: NetworkQueue<String> = NetworkQueue::new(kind);
        len_helper(&q, 0);
        q
    }

    #[test]
    fn new() {
        new_helper(NetworkQueueKind::MinPriority);
        new_helper(NetworkQueueKind::FirstInFirstOut);
    }

    fn push_helper(kind: NetworkQueueKind) -> NetworkQueue<String> {
        let mut q = new_helper(kind);

        q.push(String::from("First:Max"), Some(3));
        len_helper(&q, 1);
        assert!(q.contains(&String::from("First:Max")));

        q.push(String::from("Second:Mid"), Some(2));
        len_helper(&q, 2);
        assert!(q.contains(&String::from("First:Max")));
        assert!(q.contains(&String::from("Second:Mid")));

        q.push(String::from("Third:Min"), Some(1));
        len_helper(&q, 3);
        assert!(q.contains(&String::from("First:Max")));
        assert!(q.contains(&String::from("Second:Mid")));
        assert!(q.contains(&String::from("Third:Min")));
        q
    }

    #[test]
    fn push() {
        push_helper(NetworkQueueKind::MinPriority);
        push_helper(NetworkQueueKind::FirstInFirstOut);
    }

    #[test]
    fn push_equal_priority() {
        let mut q = new_helper(NetworkQueueKind::MinPriority);

        const NUM_ITEMS: i32 = 10;

        // Push a bunch of items with the same priority.
        for i in 0..NUM_ITEMS {
            q.push(format!("One:{i}"), Some(1));
            q.push(format!("Two:{i}"), Some(2));
            q.push(format!("Zero:{i}"), Some(0));
        }

        // Make sure they exist.
        for i in 0..NUM_ITEMS {
            assert!(q.contains(&format!("One:{i}")));
            assert!(q.contains(&format!("Two:{i}")));
            assert!(q.contains(&format!("Zero:{i}")));
        }

        // Lower priority first, then the ones pushed first should be popped first.
        for i in 0..NUM_ITEMS {
            assert_eq!(q.pop(), Some(format!("Zero:{i}")));
        }
        for i in 0..NUM_ITEMS {
            assert_eq!(q.pop(), Some(format!("One:{i}")));
        }
        for i in 0..NUM_ITEMS {
            assert_eq!(q.pop(), Some(format!("Two:{i}")));
        }
    }

    #[test]
    fn pop() {
        let mut q = push_helper(NetworkQueueKind::MinPriority);
        len_helper(&q, 3);
        assert!(q.contains(&String::from("First:Max")));
        assert!(q.contains(&String::from("Second:Mid")));
        assert!(q.contains(&String::from("Third:Min")));
        assert_eq!(q.pop(), Some(String::from("Third:Min")));
        len_helper(&q, 2);
        assert!(q.contains(&String::from("First:Max")));
        assert!(q.contains(&String::from("Second:Mid")));
        assert!(!q.contains(&String::from("Third:Min")));
        assert_eq!(q.pop(), Some(String::from("Second:Mid")));
        len_helper(&q, 1);
        assert!(q.contains(&String::from("First:Max")));
        assert!(!q.contains(&String::from("Second:Mid")));
        assert!(!q.contains(&String::from("Third:Min")));
        assert_eq!(q.pop(), Some(String::from("First:Max")));
        len_helper(&q, 0);
        assert!(!q.contains(&String::from("First:Max")));
        assert!(!q.contains(&String::from("Second:Mid")));
        assert!(!q.contains(&String::from("Third:Min")));

        let mut q = push_helper(NetworkQueueKind::FirstInFirstOut);
        len_helper(&q, 3);
        assert!(q.contains(&String::from("First:Max")));
        assert!(q.contains(&String::from("Second:Mid")));
        assert!(q.contains(&String::from("Third:Min")));
        assert_eq!(q.pop(), Some(String::from("First:Max")));
        len_helper(&q, 2);
        assert!(!q.contains(&String::from("First:Max")));
        assert!(q.contains(&String::from("Second:Mid")));
        assert!(q.contains(&String::from("Third:Min")));
        assert_eq!(q.pop(), Some(String::from("Second:Mid")));
        len_helper(&q, 1);
        assert!(!q.contains(&String::from("First:Max")));
        assert!(!q.contains(&String::from("Second:Mid")));
        assert!(q.contains(&String::from("Third:Min")));
        assert_eq!(q.pop(), Some(String::from("Third:Min")));
        len_helper(&q, 0);
        assert!(!q.contains(&String::from("First:Max")));
        assert!(!q.contains(&String::from("Second:Mid")));
        assert!(!q.contains(&String::from("Third:Min")));
    }

    #[test]
    fn none_priority() {
        let mut q = new_helper(NetworkQueueKind::MinPriority);
        assert_eq!(q.try_push(String::from("Item1"), Some(1)), Ok(()));
        assert_eq!(
            q.try_push(String::from("Item2"), None),
            Err(PushError::NonePriority)
        );

        let mut q = new_helper(NetworkQueueKind::FirstInFirstOut);
        assert_eq!(q.try_push(String::from("Item1"), Some(1)), Ok(()));
        assert_eq!(q.try_push(String::from("Item2"), None), Ok(()));
    }

    #[test]
    fn already_queued() {
        let mut q = new_helper(NetworkQueueKind::MinPriority);
        assert_eq!(q.try_push(String::from("Item1"), Some(1)), Ok(()));
        assert_eq!(
            q.try_push(String::from("Item1"), Some(2)),
            Err(PushError::AlreadyQueued)
        );

        let mut q = new_helper(NetworkQueueKind::FirstInFirstOut);
        assert_eq!(q.try_push(String::from("Item1"), Some(1)), Ok(()));
        assert_eq!(
            q.try_push(String::from("Item1"), Some(2)),
            Err(PushError::AlreadyQueued)
        );
    }

    #[test]
    fn rc_wrapped_items() {
        // This test is very specific to the current implementation which clones an item on push,
        // and it will have to be changed if we use an Rc or Arc internally rather than relying and
        // the caller to use it.
        let item = Rc::new(String::from("Item"));
        assert_eq!(Rc::strong_count(&item), 1);

        let mut q = NetworkQueue::new(NetworkQueueKind::MinPriority);
        q.push(item.clone(), Some(1));
        assert!(q.contains(&item));
        assert_eq!(Rc::strong_count(&item), 3);

        assert_eq!(
            q.try_push(item.clone(), Some(2)),
            Err(PushError::AlreadyQueued)
        );
        assert!(q.contains(&item));
        assert_eq!(Rc::strong_count(&item), 3);

        assert_eq!(q.pop(), Some(item.clone()));
        assert!(!q.contains(&item));
        assert_eq!(Rc::strong_count(&item), 1);
    }
}
