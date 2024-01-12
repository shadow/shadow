use std::cell::RefCell;
use std::ops::DerefMut;

use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;

use crate::cshadow as c;
use crate::host::descriptor::listener::StateEventSource;
use crate::utility::callback_queue::CallbackQueue;

/// An event source stored by a `LegacyFile`.
#[allow(non_camel_case_types)]
pub type RootedRefCell_StateEventSource = RootedRefCell<StateEventSource>;

thread_local! {
    static C_CALLBACK_QUEUE: RefCell<Option<CallbackQueue>> = const { RefCell::new(None) };
}

/// Helper function to initialize and run a global thread-local callback queue. This is a hack so
/// that C [`LegacyFile`](crate::cshadow::LegacyFile)s can queue listener callbacks using
/// `notify_listeners_with_global_cb_queue`. This is primarily for [`TCP`](crate::cshadow::TCP)
/// objects, and should not be used with Rust file objects.
///
/// Just like
/// [`CallbackQueue::queue_and_run`](crate::utility::callback_queue::CallbackQueue::queue_and_run),
/// the closure should make any borrows of the file object, rather than making any borrows outside
/// of the closure.
pub fn with_global_cb_queue<T>(f: impl FnOnce() -> T) -> T {
    C_CALLBACK_QUEUE.with(|cb_queue| {
        if cb_queue.borrow().is_some() {
            // we seem to be in a nested `with_global_cb_queue()` call, so just run the closure with
            // the existing queue
            return f();
        }

        // set the global queue
        assert!(cb_queue
            .borrow_mut()
            .replace(CallbackQueue::new())
            .is_none());

        let rv = f();

        // run the queued callbacks
        loop {
            // take and replace the global queue since callbacks may try to add new callbacks to the
            // global queue as we're running old callbacks
            let mut queue_to_run = cb_queue.borrow_mut().replace(CallbackQueue::new()).unwrap();
            if queue_to_run.is_empty() {
                // no new callbacks were added, so we're done
                break;
            }
            queue_to_run.run();
        }

        assert!(cb_queue.borrow_mut().take().is_some());

        rv
    })
}
mod export {
    use super::*;

    use std::net::Ipv4Addr;

    use crate::core::worker;
    use crate::host::descriptor::socket::inet::InetSocket;
    use crate::host::descriptor::FileSignals;
    use crate::host::host::Host;

    /// Notify listeners using the global callback queue. If the queue hasn't been set using
    /// [`with_global_cb_queue`], the listeners will be notified here before returning.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn notify_listeners_with_global_cb_queue(
        event_source: *const RootedRefCell_StateEventSource,
        status: c::Status,
        changed: c::Status,
        signals: FileSignals,
    ) {
        let event_source = unsafe { event_source.as_ref() }.unwrap();

        with_global_cb_queue(|| {
            C_CALLBACK_QUEUE.with(|cb_queue| {
                let mut cb_queue = cb_queue.borrow_mut();
                // must not be `None` since it will be set to `Some` by `with_global_cb_queue`
                let cb_queue = cb_queue.deref_mut().as_mut().unwrap();

                worker::Worker::with_active_host(|host| {
                    let mut event_source = event_source.borrow_mut(host.root());
                    event_source.notify_listeners(status, changed, signals, cb_queue)
                })
                .unwrap();
            });
        });
    }

    /// Tell the host that the socket wants to send packets using the global callback queue. If the
    /// queue hasn't been set using [`with_global_cb_queue`], the host will be notified here before
    /// returning. Takes ownership of `inetSocket` (will free/drop).
    #[no_mangle]
    pub unsafe extern "C-unwind" fn socket_wants_to_send_with_global_cb_queue(
        host: *const Host,
        socket: *mut InetSocket,
        ip: libc::in_addr_t,
    ) {
        let host = unsafe { host.as_ref() }.unwrap();
        let ip = Ipv4Addr::from(u32::from_be(ip));

        let host_id = host.id();

        with_global_cb_queue(|| {
            C_CALLBACK_QUEUE.with(|cb_queue| {
                let mut cb_queue = cb_queue.borrow_mut();
                // must not be `None` since it will be set to `Some` by `with_global_cb_queue`
                let cb_queue = cb_queue.deref_mut().as_mut().unwrap();

                cb_queue.add(move |_cb_queue| {
                    worker::Worker::with_active_host(|host| {
                        assert_eq!(host.id(), host_id);
                        let socket = unsafe { Box::from_raw(socket) };
                        host.notify_socket_has_packets(ip, &socket);
                    })
                    .unwrap();
                });
            });
        });
    }
}
