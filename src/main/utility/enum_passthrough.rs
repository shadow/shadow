/** A macro that defines a function for an enum, calling the same function on all enum variants.

For example, the usage:

```ignore
enum_passthrough!(self, (cb_queue), Pipe, Socket;
    pub fn close(&mut self, cb_queue: &mut EventQueue) -> SyscallResult
);
```

expands to:

```ignore
pub fn close(&mut self, cb_queue: &mut EventQueue) -> SyscallResult {
    match self {
        Self::Pipe(x) => x.close(cb_queue),
        Self::Socket(x) => x.close(cb_queue),
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

```ignore
enum_passthrough_generic!(self, (bytes, offset, cb_queue), Pipe, Socket;
    pub fn read<W>(&mut self, bytes: W, offset: libc::off_t, cb_queue: &mut EventQueue) -> SyscallResult
    where W: std::io::Write + std::io::Seek
);
```
**/
// This is currently unused, but keeping around for now since we may want it again in the future.
#[allow(unused_macros)]
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

```ignore
enum_passthrough_into!(self, (cb_queue), Pipe, Socket;
    pub fn close(&mut self, cb_queue: &mut EventQueue) -> SyscallResult
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
