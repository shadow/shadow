This is a proof of concept for safe, efficient object graphs in Rust.  It is
inspired by the concurrency model used in
[Shadow's](https://github.com/shadow/shadow) C implementation, and is intended
as a potential path toward migrating Shadow's C code to Rust without first
having to extensively refactor and/or introduce a lot of atomic operations
(which are intrinsically moderately expensive, and can result in additional
cache misses, and prevent the compiler from reordering some code).

Shadow simulates a network of Hosts, each of which has a lock associated with
it.  Inside the Hosts are a graph of ref-counted objects. They are meant to only
be accessed with the corresponding Host lock held, and do *not* take additional
locks when manipulating the reference counts.

Hosts are sent across Worker threads over the course of a simulation.

Translating this model to Rust, we can't simply use `Rc` for the reference counts,
since the Hosts would then not be `Send`.

We could use `Arc`, but this would introduce a lot of new costly atomic operations.

Here we encode Shadow's original safety model into Rust's type system. Shadow's
host lock is replaced with a `crate::Root`, which can be locked.  Instances of
`crate::rc::RootedRc` and `crate::refcell::RootedRefCell` are associated with a
`Root`, and require the caller to prove they hold that `Root`'s lock; this allows
them to avoid having to perform any additional atomic operations.

It's not clear to me yet whether the performance gains are generally worth the
extra complexity vs. just using more "mainstream" `Send` and `Sync` equivalents.
In the case of shadow, and maybe other projects being ported from C, the idea is
to allow porting C code to Rust code in a relatively straightforward way without
having to worry too much about "death by a thousand cuts" performance
degradation from introducing many new atomic operations. Once we have used this
technique to migrate most of shadow's code to Rust, the plan will be to compare
macro benchmarks with this crate's internals replaced by the more mainstream
thread-safe equivalents.

## Performance And Send/Sync

`RootedRc` is roughly half the cost of `Arc`, and about the same as `Rc`. From fastest to slowest:

| benchmark | time | Send | Sync |
| -------- | ------ | -- | -- |
| **clone and drop/RootedRc** |  15.634 ns | Send where T: Sync + Send | Sync where T: Sync + Send |
| clone and drop/Rc                  | 16.527 ns | !Send | !Sync |
| clone and drop/Arc                  | 31.171 ns | Send where T: Sync + Send |  Sync where T: Sync + Send |

`RootedRefCell` is slightly slower than `RefCell`, as expected, but significantly faster
than the next fastest thread-safe equivalent, `AtomicRefCell`.

From fastest to slowest:

| benchmark | time | Send | Sync |
| -------- | ------ | -- | -- |
| borrow_mut/RefCell       | 1.5223 ns | Send where T: Send | !Sync |
| **borrow_mut/RootedRefCell** | 1.8411 ns | Send where T: Send | Sync where T: Send |
| borrow_mut/AtomicRefCell | 6.6425 ns | Send where T: Send | Sync where T: Send |
| borrow_mut/parking_lot::Mutex | 10.848 ns | Send where T: Send | Sync where T: Send |
| borrow_mut/Mutex         | 12.666 ns | Send where T: Send | Sync where T: Send |

Benchmark sources are in `benches` and can be run with `cargo bench`.

## Usage and testing

There are some examples of intended usage in the `examples` directory.

See `maint/checks` for scripts to run tests, examples, miri, etc.

`cargo bench` runs the included benchmarks.

## Status

This is currently a sketch for discussion and analysis. It needs more review
and testing to validate soundness.