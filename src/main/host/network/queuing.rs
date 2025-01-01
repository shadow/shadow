use std::cmp::{Ordering, Reverse};
use std::collections::{BinaryHeap, HashSet, VecDeque};
use std::fmt::Debug;
use std::hash::Hash;

/// An item wrapper that allows us to implement a min-heap WRT item priority. Our implementation of
/// the `Ord` trait is such that the item containing the smallest priority (across all items) will
/// be dequeued first. This wrapper guarantees that item priority does not change while the item is
/// queued; to change an item's priority, it must be dequeued and then re-enqueued.
struct Prioritized<T> {
    item: T,
    priority: u64,
}

impl<T> Ord for Prioritized<T> {
    fn cmp(&self, other: &Self) -> Ordering {
        // This ordering is intended to implement a min-heap based on item priority.
        // `Reverse` enforces that smaller values have precedence over larger values.
        Reverse(self.priority).cmp(&Reverse(other.priority))
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
        // PartialEq must be consistent with PartialOrd.
        self.partial_cmp(other) == Some(Ordering::Equal)
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
enum QueuingDiscipline<T> {
    /// See `NetworkQueueKind::MinPriority`.
    MinPriority(BinaryHeap<Prioritized<T>>),
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
            NetworkQueueKind::MinPriority => QueuingDiscipline::MinPriority(BinaryHeap::new()),
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
            QueuingDiscipline::MinPriority(heap) => heap.clear(),
            QueuingDiscipline::FirstInFirstOut(deque) => deque.clear(),
        }
        self.membership.clear();
    }

    /// Returns true if the queue contains the item, and false otherwise.
    pub fn contains(&self, item: &T) -> bool {
        self.membership.contains(item)
    }

    /// Returns true if the queue does not contain any items.
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
            QueuingDiscipline::MinPriority(heap) => heap.pop().map(|x| x.item),
            QueuingDiscipline::FirstInFirstOut(deque) => deque.pop_front(),
        }
        .inspect(|x| {
            assert!(self.membership.remove(x));
        })
    }

    /// Pushes an item into the queue, internally cloning it to
    /// support membership checks. It may be useful to wrap items with a `std::rc::Rc` or
    /// `std::sync::Arc` before calling this function.
    ///
    /// # Panics
    /// This function panics if the queue was configured with `NetworkQueueKind::MinPriority` and
    /// `maybe_priority` is `None`.
    pub fn push(&mut self, item: T, maybe_priority: Option<u64>) {
        self.try_push(item, maybe_priority).unwrap()
    }

    /// Tries to push an item into the queue. If successful, the item is internally cloned to
    /// support membership checks, so it may be useful to wrap items with a `std::rc::Rc` or
    /// `std::sync::Arc` before calling this function. If unsuccessful, a `PushError` is returned
    /// encoding the reason for the failure.
    pub fn try_push(&mut self, item: T, maybe_priority: Option<u64>) -> Result<(), PushError> {
        if self.contains(&item) {
            Err(PushError::AlreadyQueued)
        } else {
            assert!(self.membership.insert(item.clone()));
            match &mut self.queue {
                QueuingDiscipline::MinPriority(heap) => {
                    if let Some(priority) = maybe_priority {
                        heap.push(Prioritized { item, priority })
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
            QueuingDiscipline::MinPriority(heap) => assert_eq!(heap.len(), len),
            QueuingDiscipline::FirstInFirstOut(deque) => assert_eq!(deque.len(), len),
        };
        assert_eq!(q.membership.len(), len);
        assert_eq!(q.len(), len);
        if len == 0 {
            assert!(q.is_empty());
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
