use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::utility::HostTreePointer;

use super::host::Host;

/// An object that listens for status changes.
///
/// ~Deprecated: In new code consider using [`crate::host::descriptor::StateEventSource`],
/// which supports Rust closures or directly takes [`crate::cshadow::StatusListener`].
#[derive(Debug)]
pub struct StatusListener {
    ptr: HostTreePointer<cshadow::StatusListener>,
}

impl Ord for StatusListener {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        Worker::with_active_host(|host| {
            // SAFETY: These pointers do not escape the `with_active_host` closure.
            let lhs = unsafe { self.ptr.ptr_with_host(host) };
            let rhs = unsafe { other.ptr.ptr_with_host(host) };

            match unsafe { cshadow::status_listener_compare(lhs.cast(), rhs.cast()) } {
                -1 => std::cmp::Ordering::Less,
                0 => std::cmp::Ordering::Equal,
                1 => std::cmp::Ordering::Greater,
                other => panic!("Unexpected comparison result: {other}"),
            }
        })
        .unwrap()
    }
}

impl PartialOrd for StatusListener {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Eq for StatusListener {}

impl PartialEq for StatusListener {
    fn eq(&self, other: &Self) -> bool {
        matches!(self.cmp(other), std::cmp::Ordering::Equal)
    }
}

impl StatusListener {
    /// Create an owned reference from `ptr`. Reference count is incremented on
    /// creation, and decremented on Drop.
    ///
    /// # Safety
    ///
    /// `ptr` must be safely dereferenceable.
    pub unsafe fn clone_from_c(host: &Host, ptr: *mut cshadow::StatusListener) -> Self {
        unsafe { cshadow::statuslistener_ref(ptr) };
        let ptr = HostTreePointer::new_for_host(host.id(), ptr);
        Self { ptr }
    }

    /// Create a [`StatusListener`] with the given notification function.
    pub fn new(host: &Host, notify: impl Fn(&Host) + Send + Sync + 'static) -> Self {
        extern "C" fn notify_fn(
            callback_object: *mut std::ffi::c_void,
            _callback_arg: *mut std::ffi::c_void,
        ) {
            let task: *const TaskRef = callback_object.cast();
            let task = unsafe { &*task };
            Worker::with_active_host(|host| task.execute(host)).unwrap()
        }
        extern "C" fn object_free_fn(callback_object: *mut std::ffi::c_void) {
            let task: *mut TaskRef = callback_object.cast();
            drop(unsafe { Box::from_raw(task) })
        }

        let callback_obj = Box::into_raw(Box::new(TaskRef::new(notify)));
        let status_listener = unsafe {
            cshadow::statuslistener_new(
                Some(notify_fn),
                callback_obj.cast::<std::ffi::c_void>(),
                Some(object_free_fn),
                std::ptr::null_mut(),
                None,
                host,
            )
        };
        Self {
            ptr: HostTreePointer::new_for_host(host.id(), status_listener),
        }
    }

    /// Called when a transition (bit flip) occurred on
    /// at least one of its status bits. (This function should only be called
    /// by status owners, i.e., the descriptor or futex base classes.)
    /// If this listener is monitoring (via setMonitorStatus) any of the status bits
    /// that just transitioned, then this function will trigger a notification
    /// via the callback supplied to the new func.
    pub fn handle_status_change(
        &self,
        host: &Host,
        current: cshadow::Status,
        transitions: cshadow::Status,
    ) {
        unsafe {
            cshadow::statuslistener_onStatusChanged(
                self.ptr.ptr_with_host(host),
                current,
                transitions,
            )
        }
    }

    /// Set the status bits that we should monitor for transitions (flips),
    /// and a filter that specifies which flips should cause the callback
    /// to be invoked.
    pub fn set_monitor_status(
        &self,
        host: &Host,
        status: cshadow::Status,
        filter: cshadow::StatusListenerFilter,
    ) {
        unsafe {
            cshadow::statuslistener_setMonitorStatus(self.ptr.ptr_with_host(host), status, filter)
        }
    }
}

impl Drop for StatusListener {
    fn drop(&mut self) {
        unsafe { cshadow::statuslistener_unref(self.ptr.ptr()) };
    }
}
