use vasi::VirtualAddressSpaceIndependent;

use crate::util::NoTypeInference;

/// Used to indicate an untyped `ForeignPtr` in C code. We use the unit type as the generic rather
/// than `libc::c_void` since the unit type is zero-sized and `libc::c_void` has a size of 1 byte,
/// and this prevents us from accidentally accessing an "untyped" pointer.
pub type UntypedForeignPtr = ForeignPtr<()>;

/// Represents a pointer to a virtual address in plugin memory. The pointer is not guaranteed to be
/// valid or aligned.
///
/// When passing a `ForeignPtr` to/from C you should use [`UntypedForeignPtr`], which is mapped to
/// the untyped instance `ForeignPtr<()>` in both Rust and C.
///
/// This type derives the `VirtualAddressSpaceIndependent` trait, since it is
/// specifically meant to transfer pointers between processes. However it is of
/// course unsafe to turn its inner value back into a pointer and dereference
/// from a different virtual address space than where the pointer originated.
// do not change the definition of `ForeignPtr` without changing the C definition of
// `UntypedForeignPtr` in the build script
#[derive(Eq, PartialEq, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ForeignPtr<T> {
    val: usize,
    _phantom: std::marker::PhantomData<T>,
}

impl<T> ForeignPtr<T> {
    fn new_with_type_inference(val: usize) -> Self {
        Self {
            val,
            _phantom: Default::default(),
        }
    }

    pub fn null() -> Self {
        // this will be an invalid pointer so we don't really care what type rust will infer for
        // this `ForeignPtr`
        Self::new_with_type_inference(0)
    }

    /// Cast from `ForeignPtr<T>` to `ForeignPtr<U>`.
    ///
    /// This uses the [`NoTypeInference`] trait to prevent rust from inferring the target type of
    /// the cast, which will hopefully help prevent accidental invalid casts.
    ///
    /// This does not check pointer alignment.
    ///
    /// Example:
    ///
    /// ```
    /// # use shadow_shim_helper_rs::syscall_types::ForeignPtr;
    /// let ptr: ForeignPtr<u16> = ForeignPtr::null();
    /// // cast to a u8 pointer
    /// let ptr = ptr.cast::<u8>();
    /// ```
    ///
    /// If the generic argument is omitted, it will fail to compile:
    ///
    /// ```compile_fail
    /// # use shadow_shim_helper_rs::syscall_types::ForeignPtr;
    /// let ptr: ForeignPtr<u16> = ForeignPtr::null();
    /// // cast to a u8 pointer
    /// let ptr: ForeignPtr<u8> = ptr.cast();
    /// ```
    pub fn cast<U: NoTypeInference>(&self) -> ForeignPtr<U::This> {
        ForeignPtr::new_with_type_inference(self.val)
    }

    pub fn is_null(&self) -> bool {
        self.val == 0
    }

    /// Create a `ForeignPtr` from a raw pointer to plugin memory.
    pub fn from_raw_ptr(ptr: *mut T) -> Self {
        let val = ptr as usize;
        // the type of this `ForeignPtr` will be inferred from the pointer type
        Self::new_with_type_inference(val)
    }

    /// Add an offset to a pointer. `count` is in units of `T`.
    pub fn add(&self, count: usize) -> Self {
        let val = self.val;
        Self::new_with_type_inference(val + count * std::mem::size_of::<T>())
    }

    /// Subtract an offset from a pointer. `count` is in units of `T`.
    pub fn sub(&self, count: usize) -> Self {
        let val = self.val;
        Self::new_with_type_inference(val - count * std::mem::size_of::<T>())
    }
}

impl<T> From<ForeignPtr<T>> for usize {
    fn from(v: ForeignPtr<T>) -> Self {
        v.val
    }
}

impl From<usize> for ForeignPtr<()> {
    fn from(v: usize) -> ForeignPtr<()> {
        ForeignPtr {
            val: v,
            _phantom: Default::default(),
        }
    }
}

impl From<u64> for ForeignPtr<()> {
    fn from(v: u64) -> ForeignPtr<()> {
        ForeignPtr {
            val: v.try_into().unwrap(),
            _phantom: Default::default(),
        }
    }
}

impl<T> From<ForeignPtr<T>> for u64 {
    fn from(v: ForeignPtr<T>) -> Self {
        v.val.try_into().unwrap()
    }
}

impl<T> Copy for ForeignPtr<T> {}

impl<T> Clone for ForeignPtr<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T> std::fmt::Debug for ForeignPtr<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // this will also show the generic type
        f.debug_struct(std::any::type_name::<Self>())
            .field("val", &self.val)
            .finish()
    }
}

impl<T> std::fmt::Pointer for ForeignPtr<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let ptr = self.val as *const libc::c_void;
        std::fmt::Pointer::fmt(&ptr, f)
    }
}

/// Represents a pointer to a *physical* address in plugin memory.
#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct ManagedPhysicalMemoryAddr {
    val: usize,
}

impl From<ManagedPhysicalMemoryAddr> for usize {
    fn from(v: ManagedPhysicalMemoryAddr) -> usize {
        v.val
    }
}

impl From<usize> for ManagedPhysicalMemoryAddr {
    fn from(v: usize) -> ManagedPhysicalMemoryAddr {
        ManagedPhysicalMemoryAddr { val: v }
    }
}

impl From<u64> for ManagedPhysicalMemoryAddr {
    fn from(v: u64) -> ManagedPhysicalMemoryAddr {
        ManagedPhysicalMemoryAddr {
            val: v.try_into().unwrap(),
        }
    }
}

impl From<ManagedPhysicalMemoryAddr> for u64 {
    fn from(v: ManagedPhysicalMemoryAddr) -> u64 {
        v.val.try_into().unwrap()
    }
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct SysCallArgs {
    // SYS_* from sys/syscall.h.
    // (mostly included from
    // /usr/include/x86_64-linux-gnu/bits/syscall.h)
    pub number: libc::c_long,
    pub args: [SysCallReg; 6],
}

impl SysCallArgs {
    pub fn get(&self, i: usize) -> SysCallReg {
        self.args[i]
    }
    pub fn number(&self) -> i64 {
        self.number
    }
}

/// A register used for input/output in a syscall.
#[derive(Copy, Clone, Eq, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub union SysCallReg {
    as_i64: i64,
    as_u64: u64,
    as_ptr: UntypedForeignPtr,
}
// SysCallReg and all of its fields must be transmutable with a 64 bit integer.
// TODO: Store as a single `u64` and explicitly transmute in the conversion
// operations.  This requires getting rid of the direct field access in our C
// code.
static_assertions::assert_eq_align!(SysCallReg, u64);
static_assertions::assert_eq_size!(SysCallReg, u64);

impl PartialEq for SysCallReg {
    fn eq(&self, other: &Self) -> bool {
        unsafe { self.as_u64 == other.as_u64 }
    }
}

impl From<u64> for SysCallReg {
    fn from(v: u64) -> Self {
        Self { as_u64: v }
    }
}

impl From<SysCallReg> for u64 {
    fn from(v: SysCallReg) -> u64 {
        unsafe { v.as_u64 }
    }
}

impl From<u32> for SysCallReg {
    fn from(v: u32) -> Self {
        Self { as_u64: v as u64 }
    }
}

impl From<SysCallReg> for u32 {
    fn from(v: SysCallReg) -> u32 {
        (unsafe { v.as_u64 }) as u32
    }
}

impl From<usize> for SysCallReg {
    fn from(v: usize) -> Self {
        Self { as_u64: v as u64 }
    }
}

impl From<SysCallReg> for usize {
    fn from(v: SysCallReg) -> usize {
        unsafe { v.as_u64 as usize }
    }
}

impl From<isize> for SysCallReg {
    fn from(v: isize) -> Self {
        Self { as_i64: v as i64 }
    }
}

impl From<SysCallReg> for isize {
    fn from(v: SysCallReg) -> isize {
        unsafe { v.as_i64 as isize }
    }
}

impl From<i64> for SysCallReg {
    fn from(v: i64) -> Self {
        Self { as_i64: v }
    }
}

impl From<SysCallReg> for i64 {
    fn from(v: SysCallReg) -> i64 {
        unsafe { v.as_i64 }
    }
}

impl From<i32> for SysCallReg {
    fn from(v: i32) -> Self {
        Self { as_i64: v as i64 }
    }
}

impl From<SysCallReg> for i32 {
    fn from(v: SysCallReg) -> i32 {
        (unsafe { v.as_i64 }) as i32
    }
}

impl TryFrom<SysCallReg> for u8 {
    type Error = <u8 as TryFrom<u64>>::Error;

    fn try_from(v: SysCallReg) -> Result<u8, Self::Error> {
        (unsafe { v.as_u64 }).try_into()
    }
}

impl TryFrom<SysCallReg> for u16 {
    type Error = <u16 as TryFrom<u64>>::Error;

    fn try_from(v: SysCallReg) -> Result<u16, Self::Error> {
        (unsafe { v.as_u64 }).try_into()
    }
}

impl TryFrom<SysCallReg> for i8 {
    type Error = <i8 as TryFrom<i64>>::Error;

    fn try_from(v: SysCallReg) -> Result<i8, Self::Error> {
        (unsafe { v.as_i64 }).try_into()
    }
}

impl TryFrom<SysCallReg> for i16 {
    type Error = <i16 as TryFrom<i64>>::Error;

    fn try_from(v: SysCallReg) -> Result<i16, Self::Error> {
        (unsafe { v.as_i64 }).try_into()
    }
}

impl<T> From<ForeignPtr<T>> for SysCallReg {
    fn from(v: ForeignPtr<T>) -> Self {
        Self {
            as_ptr: v.cast::<()>(),
        }
    }
}

impl<T> From<SysCallReg> for ForeignPtr<T> {
    fn from(v: SysCallReg) -> Self {
        // This allows rust to infer the type for the cast. This isn't ideal since we generally want
        // to require that the user be explicit about casts (for example we use the
        // `NoTypeInference` trait on `ForeignPtr::cast`), but we need this type inference so that
        // `SyscallHandlerFn` can convert the `SysCallReg` to the correct pointer type in syscall
        // handler arguments.
        (unsafe { v.as_ptr }).cast::<T>()
    }
}

// Useful for syscalls whose strongly-typed wrappers return some Result<(), ErrType>
impl From<()> for SysCallReg {
    fn from(_: ()) -> SysCallReg {
        SysCallReg { as_i64: 0 }
    }
}

impl std::fmt::Debug for SysCallReg {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SysCallReg")
            .field("as_i64", unsafe { &self.as_i64 })
            .field("as_u64", unsafe { &self.as_u64 })
            .field("as_ptr", unsafe { &self.as_ptr })
            .finish()
    }
}

// implement conversions from `SysCallReg`

impl TryFrom<SysCallReg> for nix::fcntl::OFlag {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SysCallReg> for nix::sys::eventfd::EfdFlags {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SysCallReg> for nix::sys::socket::AddressFamily {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_i32(reg.into()).ok_or(())
    }
}

impl TryFrom<SysCallReg> for nix::sys::socket::MsgFlags {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SysCallReg> for nix::sys::stat::Mode {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SysCallReg> for nix::sys::mman::ProtFlags {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SysCallReg> for nix::sys::mman::MapFlags {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SysCallReg> for nix::sys::mman::MRemapFlags {
    type Error = ();
    fn try_from(reg: SysCallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}
