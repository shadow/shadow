use std::collections::{BTreeMap, VecDeque};
use std::net::Ipv4Addr;

use shadow_shim_helper_rs::emulated_time::EmulatedTime;

use crate::network::packet::PacketRc;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TaskKind {
    Deliver(PacketRc),
    TxGlobal,
    RxGlobal,
    TxDevice(Ipv4Addr),
    RxDevice(Ipv4Addr),
    ProcessQueue,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Task {
    pub when: EmulatedTime,
    pub kind: TaskKind,
}

#[derive(Clone, Debug, Default)]
pub struct TaskQueue {
    // Maps time to a fifo queue of tasks at that time.
    tasks: BTreeMap<EmulatedTime, VecDeque<Task>>,
}

impl TaskQueue {
    /// Create an empty task queue.
    pub fn new() -> Self {
        Self {
            tasks: BTreeMap::new(),
        }
    }

    /// Pushes a new task of the given kind and time into the queue.
    pub fn schedule(&mut self, kind: TaskKind, when: EmulatedTime) {
        self.push(Task { kind, when });
    }

    /// Adds the task to the queue.
    pub fn push(&mut self, task: Task) {
        self.tasks.entry(task.when).or_default().push_back(task);
    }

    /// Returns a reference to the task with the earliest time.
    pub fn peek(&self) -> Option<&Task> {
        // Get the smallest time, and the front of that time's fifo queue.
        self.tasks
            .first_key_value()
            .and_then(|(_, fifo_queue)| fifo_queue.front())
    }

    /// Removes and returns the task with the earliest time.
    pub fn pop(&mut self) -> Option<Task> {
        // Get the entry for the earliest time.
        let mut entry = self.tasks.first_entry()?;

        // Take the next task from the front of the fifo queue.
        let fifo_queue = entry.get_mut();
        let task = fifo_queue.pop_front();

        // If the queue for this time is now empty, remove the entry.
        if fifo_queue.is_empty() {
            entry.remove_entry();
        }

        task
    }

    // Returns true if at least one task exists in the queue at the given time.
    #[cfg(test)]
    pub fn contains_time(&self, when: &EmulatedTime) -> bool {
        self.tasks.contains_key(when)
    }

    /// Returns the number of tasks in the queue.
    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.tasks.values().map(|q| q.len()).sum()
    }

    /// Returns true if no tasks remain in the queue.
    #[cfg(test)]
    pub fn is_empty(&self) -> bool {
        self.tasks.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use std::net::Ipv4Addr;

    use shadow_shim_helper_rs::emulated_time::EmulatedTime;
    use shadow_shim_helper_rs::simulation_time::SimulationTime;

    use super::{TaskKind, TaskQueue};

    // Helper function to create an EmulatedTime object.
    fn emutime(seconds: u64) -> EmulatedTime {
        EmulatedTime::from_abs_simtime(SimulationTime::from_secs(seconds))
    }

    #[test]
    fn test_time_ordering() {
        let mut queue = TaskQueue::new();

        // Push tasks out of order.
        queue.schedule(TaskKind::TxGlobal, emutime(100));
        queue.schedule(TaskKind::TxGlobal, emutime(50));
        queue.schedule(TaskKind::TxGlobal, emutime(75));

        // Should pop in order.
        assert_eq!(queue.pop().unwrap().when, emutime(50));
        assert_eq!(queue.pop().unwrap().when, emutime(75));
        assert_eq!(queue.pop().unwrap().when, emutime(100));
    }

    #[test]
    fn test_fifo_ordering_for_same_time() {
        let mut queue = TaskQueue::new();
        let time = emutime(100);

        let kind1 = TaskKind::TxGlobal;
        let kind2 = TaskKind::RxGlobal;
        let kind3 = TaskKind::TxDevice(Ipv4Addr::LOCALHOST);

        // Push multiple tasks for the same time.
        queue.schedule(kind1.clone(), time);
        queue.schedule(kind2.clone(), time);
        queue.schedule(kind3.clone(), time);

        // Should pop in the order they were pushed.
        let first = queue.pop().unwrap();
        let second = queue.pop().unwrap();
        let third = queue.pop().unwrap();

        assert_eq!(first.when, time);
        assert_eq!(second.when, time);
        assert_eq!(third.when, time);

        assert_eq!(first.kind, kind1);
        assert_eq!(second.kind, kind2);
        assert_eq!(third.kind, kind3);
    }

    #[test]
    fn test_queue_cleanup_and_length() {
        let mut queue = TaskQueue::new();

        let time1 = emutime(10);
        let time2 = emutime(20);
        let kind = TaskKind::TxGlobal;

        // Push 2 tasks at time1, 1 task at time2
        queue.schedule(kind.clone(), time1);
        queue.schedule(kind.clone(), time1);
        queue.schedule(kind, time2);

        assert_eq!(queue.len(), 3);

        // Popping should update length correctly.
        queue.pop();
        assert_eq!(queue.len(), 2);
        queue.pop();
        assert_eq!(queue.len(), 1);

        // Verify that the entry for time1 was removed (not just empty).
        assert!(!queue.contains_time(&time1));
        assert!(queue.contains_time(&time2));

        // Popping the final task should empty the queue.
        queue.pop();
        assert!(queue.is_empty());
        assert_eq!(queue.len(), 0);
    }
}
