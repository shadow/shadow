pub mod byte_queue;
pub mod childpid_watcher;
pub mod counter;
pub mod event_queue;
pub mod interval_map;
pub mod notnull;
pub mod perf_timer;
pub mod pod;
pub mod proc_maps;
pub mod status_bar;
pub mod stream_len;
pub mod syscall;
pub mod time;

/// A type that allows us to make a pointer Send + Sync since there is no way
/// to add these traits to the pointer itself.
#[derive(Clone, Copy, Debug)]
pub struct SyncSendPointer<T>(pub *mut T);

unsafe impl<T> Send for SyncSendPointer<T> {}
unsafe impl<T> Sync for SyncSendPointer<T> {}

impl<T> SyncSendPointer<T> {
    /// Get the pointer.
    pub fn ptr(&self) -> *mut T {
        self.0
    }

    /// Get a mutable reference to the pointer.
    pub fn ptr_ref(&mut self) -> &mut *mut T {
        &mut self.0
    }
}

/** A macro that defines a function for an enum, calling the same function on all enum variants.

For example, the usage:

```rust
enum_passthrough!(self, (event_queue), Pipe, Socket;
    pub fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult
);
```

expands to:

```rust
pub fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult {
    match self {
        Self::Pipe(x) => x.close(event_queue),
        Self::Socket(x) => x.close(event_queue),
    }
}
```
**/
macro_rules! enum_passthrough {
    ($self:ident, $args2:tt, $($variant:ident),+; $v:vis fn $name:ident $args:tt $(-> $($rv:tt)+)?) => {
        $v fn $name $args $(-> $($rv)+)? {
            match $self {
                $(
                Self::$variant(x) => x.$name $args2,
                )*
            }
        }
    };
}

/** Like [`enum_passthrough!`], but allows generics. For example:

```rust
enum_passthrough_generic!(self, (bytes, offset, event_queue), Pipe, Socket;
    pub fn read<W>(&mut self, bytes: W, offset: libc::off_t, event_queue: &mut EventQueue) -> SyscallResult
    where W: std::io::Write + std::io::Seek
);
```
**/
macro_rules! enum_passthrough_generic {
    ($self:ident, $args2:tt, $($variant:ident),+; $v:vis fn $name:ident <$($generics:ident),+> $args:tt $(-> $($rv:tt)+)?) => {
        $v fn $name <$($generics)+> $args $(-> $($rv)+)? {
            match $self {
                $(
                Self::$variant(x) => x.$name $args2,
                )*
            }
        }
    };
}
