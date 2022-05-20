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
    ($self:ident, $args2:tt, $($variant:ident),+; $(#[$($mac:tt)+])? $v:vis fn $name:ident <$($generics:ident),+> $args:tt $(-> $($rv:tt)+)?) => {
        $(#[$($mac)+])?
        $v fn $name <$($generics)+> $args $(-> $($rv)+)? {
            match $self {
                $(
                Self::$variant(x) => x.$name $args2,
                )*
            }
        }
    };
}

/** Like [`enum_passthrough!`], but calls `into()` on the return value. For example:

```rust
enum_passthrough_into!(self, (event_queue), Pipe, Socket;
    pub fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult
);
```
**/

macro_rules! enum_passthrough_into {
    ($self:ident, $args2:tt, $($variant:ident),+; $(#[$($mac:tt)+])? $v:vis fn $name:ident $args:tt $(-> $($rv:tt)+)?) => {
        $(#[$($mac)+])?
        $v fn $name $args $(-> $($rv)+)? {
            match $self {
                $(
                Self::$variant(x) => x.$name $args2.into(),
                )*
            }
        }
    };
}
