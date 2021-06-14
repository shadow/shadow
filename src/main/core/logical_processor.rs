#![allow(unsafe_op_in_unsafe_fn, dead_code, mutable_transmutes, non_camel_case_types, non_snake_case,
         non_upper_case_globals, unused_assignments, unused_mut)]
#![register_tool(c2rust)]
#![feature(extern_types, register_tool)]
extern "C" {
    pub type _GAsyncQueue;
    #[no_mangle]
    fn g_async_queue_new() -> *mut GAsyncQueue;
    #[no_mangle]
    fn g_async_queue_unref(queue: *mut GAsyncQueue);
    #[no_mangle]
    fn g_async_queue_try_pop(queue: *mut GAsyncQueue) -> gpointer;
    #[no_mangle]
    fn g_async_queue_push_front(queue: *mut GAsyncQueue, item: gpointer);
    #[no_mangle]
    fn g_free(mem: gpointer);
    #[no_mangle]
    fn g_malloc(n_bytes: gsize) -> gpointer;
    #[no_mangle]
    fn g_malloc_n(n_blocks: gsize, n_block_bytes: gsize) -> gpointer;
    #[no_mangle]
    fn affinity_getGoodWorkerAffinity() -> libc::c_int;
}
pub type size_t = libc::c_ulong;
pub type gsize = libc::c_ulong;
pub type glong = libc::c_long;
pub type gint = libc::c_int;
pub type gpointer = *mut libc::c_void;
pub type GDestroyNotify = Option<unsafe extern "C" fn(_: gpointer) -> ()>;
pub type GAsyncQueue = _GAsyncQueue;
#[derive(Copy, Clone)]
#[repr(C)]
pub struct _LogicalProcessors {
    pub lps: *mut LogicalProcessor,
    pub n: size_t,
}
/*

 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
pub type LogicalProcessor = _LogicalProcessor;
#[derive(Copy, Clone)]
#[repr(C)]
pub struct _LogicalProcessor {
    pub cpuId: libc::c_int,
    pub readyWorkers: *mut GAsyncQueue,
    pub doneWorkers: *mut GAsyncQueue,
}
pub type LogicalProcessors = _LogicalProcessors;
#[derive(Copy, Clone)]
#[repr(C)]
pub union C2RustUnnamed {
    pub in_0: *mut libc::c_char,
    pub out: *mut gpointer,
}
#[derive(Copy, Clone)]
#[repr(C)]
pub union C2RustUnnamed_0 {
    pub in_0: *mut libc::c_char,
    pub out: *mut gpointer,
}
unsafe extern "C" fn _idx_to_ptr(mut idx: libc::c_int) -> *mut libc::c_void {
    // g_async_queue stores pointers. We need to use GINT_TO_POINTER to
    // transform the integer to a pointer. Unfortunately that translates 0 to
    // NULL, so we need to offset by 1.
    return (idx + 1 as libc::c_int) as glong as gpointer;
}
unsafe extern "C" fn _ptr_to_idx(mut ptr: *mut libc::c_void) -> libc::c_int {
    // Undo the offset added in _idx_to_ptr.
    return ptr as glong as gint - 1 as libc::c_int;
}
unsafe extern "C" fn _idx(mut lps: *mut LogicalProcessors, mut n: libc::c_int)
 -> *mut LogicalProcessor {
    let mut lp: *mut LogicalProcessor =
        &mut *(*lps).lps.offset(n as isize) as *mut LogicalProcessor;
    return lp;
}
#[no_mangle]
pub unsafe extern "C" fn lps_new(mut n: libc::c_int)
 -> *mut LogicalProcessors {
    let mut lps: *mut LogicalProcessors =
        ({
             let mut __n: gsize = 1 as libc::c_int as gsize;
             let mut __s: gsize =
                 ::std::mem::size_of::<LogicalProcessors>() as libc::c_ulong;
             let mut __p: gpointer = 0 as *mut libc::c_void;
             if __s == 1 as libc::c_int as libc::c_ulong {
                 __p = g_malloc(__n)
             } else if 0 != 0 &&
                           (__s == 0 as libc::c_int as libc::c_ulong ||
                                __n <=
                                    (9223372036854775807 as libc::c_long as
                                         libc::c_ulong).wrapping_mul(2 as
                                                                         libc::c_ulong).wrapping_add(1
                                                                                                         as
                                                                                                         libc::c_ulong).wrapping_div(__s))
              {
                 __p = g_malloc(__n.wrapping_mul(__s))
             } else { __p = g_malloc_n(__n, __s) }
             __p
         }) as *mut LogicalProcessors;
    *lps =
        {
            let mut init =
                _LogicalProcessors{lps:
                                       ({
                                            let mut __n: gsize = n as gsize;
                                            let mut __s: gsize =
                                                ::std::mem::size_of::<LogicalProcessor>()
                                                    as libc::c_ulong;
                                            let mut __p: gpointer =
                                                0 as *mut libc::c_void;
                                            if __s ==
                                                   1 as libc::c_int as
                                                       libc::c_ulong {
                                                __p = g_malloc(__n)
                                            } else if 0 != 0 &&
                                                          (__s ==
                                                               0 as
                                                                   libc::c_int
                                                                   as
                                                                   libc::c_ulong
                                                               ||
                                                               __n <=
                                                                   (9223372036854775807
                                                                        as
                                                                        libc::c_long
                                                                        as
                                                                        libc::c_ulong).wrapping_mul(2
                                                                                                        as
                                                                                                        libc::c_ulong).wrapping_add(1
                                                                                                                                        as
                                                                                                                                        libc::c_ulong).wrapping_div(__s))
                                             {
                                                __p =
                                                    g_malloc(__n.wrapping_mul(__s))
                                            } else {
                                                __p = g_malloc_n(__n, __s)
                                            }
                                            __p
                                        }) as *mut LogicalProcessor,
                                   n: n as size_t,};
            init
        };
    let mut i: libc::c_int = 0 as libc::c_int;
    while i < n {
        let mut lp: *mut LogicalProcessor =
            &mut *(*lps).lps.offset(i as isize) as *mut LogicalProcessor;
        *lp =
            {
                let mut init =
                    _LogicalProcessor{cpuId: affinity_getGoodWorkerAffinity(),
                                      readyWorkers: g_async_queue_new(),
                                      doneWorkers: g_async_queue_new(),};
                init
            };
        i += 1
    }
    return lps;
}
#[no_mangle]
pub unsafe extern "C" fn lps_free(mut lps: *mut LogicalProcessors) {
    let mut i: libc::c_int = 0 as libc::c_int;
    while (i as libc::c_ulong) < (*lps).n {
        let mut lp: *mut LogicalProcessor = _idx(lps, i);
        let mut _pp: C2RustUnnamed_0 =
            C2RustUnnamed_0{in_0: 0 as *mut libc::c_char,};
        let mut _p: gpointer = 0 as *mut libc::c_void;
        let mut _destroy: GDestroyNotify =
            ::std::mem::transmute::<Option<unsafe extern "C" fn(_:
                                                                    *mut GAsyncQueue)
                                               -> ()>,
                                    GDestroyNotify>(Some(g_async_queue_unref
                                                             as
                                                             unsafe extern "C" fn(_:
                                                                                      *mut GAsyncQueue)
                                                                 -> ()));
        _pp.in_0 =
            &mut (*lp).readyWorkers as *mut *mut GAsyncQueue as
                *mut libc::c_char;
        _p = *_pp.out;
        if !_p.is_null() {
            *_pp.out = 0 as *mut libc::c_void;
            _destroy.expect("non-null function pointer")(_p);
        }
        let mut _pp_0: C2RustUnnamed =
            C2RustUnnamed{in_0: 0 as *mut libc::c_char,};
        let mut _p_0: gpointer = 0 as *mut libc::c_void;
        let mut _destroy_0: GDestroyNotify =
            ::std::mem::transmute::<Option<unsafe extern "C" fn(_:
                                                                    *mut GAsyncQueue)
                                               -> ()>,
                                    GDestroyNotify>(Some(g_async_queue_unref
                                                             as
                                                             unsafe extern "C" fn(_:
                                                                                      *mut GAsyncQueue)
                                                                 -> ()));
        _pp_0.in_0 =
            &mut (*lp).doneWorkers as *mut *mut GAsyncQueue as
                *mut libc::c_char;
        _p_0 = *_pp_0.out;
        if !_p_0.is_null() {
            *_pp_0.out = 0 as *mut libc::c_void;
            _destroy_0.expect("non-null function pointer")(_p_0);
        }
        i += 1
    }
    g_free(lps as gpointer);
}
#[no_mangle]
pub unsafe extern "C" fn lps_n(mut lps: *mut LogicalProcessors)
 -> libc::c_int {
    return (*lps).n as libc::c_int;
}
#[no_mangle]
pub unsafe extern "C" fn lps_readyPush(mut lps: *mut LogicalProcessors,
                                       mut lpi: libc::c_int,
                                       mut worker: libc::c_int) {
    let mut lp: *mut LogicalProcessor = _idx(lps, lpi);
    g_async_queue_push_front((*lp).readyWorkers, _idx_to_ptr(worker));
}
#[no_mangle]
pub unsafe extern "C" fn lps_popWorkerToRunOn(mut lps: *mut LogicalProcessors,
                                              mut lpi: libc::c_int)
 -> libc::c_int {
    let mut lp: *mut LogicalProcessor = _idx(lps, lpi);
    let mut i: libc::c_int = 0 as libc::c_int;
    while i < lps_n(lps) {
        // Start with workers that last ran on `lpi`; if none are available
        // steal from another in round-robin order.
        let mut fromLpi: libc::c_int = (lpi + i) % lps_n(lps);
        let mut fromLp: *mut LogicalProcessor = _idx(lps, fromLpi);
        let mut worker: gpointer =
            g_async_queue_try_pop((*fromLp).readyWorkers);
        if !worker.is_null() { return _ptr_to_idx(worker) }
        i += 1
    }
    return -(1 as libc::c_int);
}
#[no_mangle]
pub unsafe extern "C" fn lps_donePush(mut lps: *mut LogicalProcessors,
                                      mut lpi: libc::c_int,
                                      mut worker: libc::c_int) {
    let mut lp: *mut LogicalProcessor = _idx(lps, lpi);
    // Push to the *front* of the queue so that the last workers to run the
    // current task, which are freshest in cache, are the first ones to run the
    // next task.
    g_async_queue_push_front((*lp).doneWorkers, _idx_to_ptr(worker));
}
#[no_mangle]
pub unsafe extern "C" fn lps_finishTask(mut lps: *mut LogicalProcessors) {
    let mut lpi: libc::c_int = 0 as libc::c_int;
    while (lpi as libc::c_ulong) < (*lps).n {
        let mut lp: *mut LogicalProcessor = _idx(lps, lpi);
        // Swap `ready` and `done` Qs
        let mut tmp: *mut GAsyncQueue = (*lp).readyWorkers;
        (*lp).readyWorkers = (*lp).doneWorkers;
        (*lp).doneWorkers = tmp;
        lpi += 1
    };
}
#[no_mangle]
pub unsafe extern "C" fn lps_cpuId(mut lps: *mut LogicalProcessors,
                                   mut lpi: libc::c_int) -> libc::c_int {
    let mut lp: *mut LogicalProcessor = _idx(lps, lpi);
    // No synchronization needed since cpus are never mutated after
    // construction.
    return (*lp).cpuId;
}
