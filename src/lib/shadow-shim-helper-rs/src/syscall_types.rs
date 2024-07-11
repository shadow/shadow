use vasi::VirtualAddressSpaceIndependent;

use crate::util::{DebugFormatter, NoTypeInference, SyncSendPointer};

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
// repr(transparent) to make this ABI-compatible with a raw pointer
#[repr(transparent)]
pub struct ForeignPtr<T> {
    val: usize,
    // `ForeignPtr` behaves as if it holds a pointer to a `T`, and we want this `ForeignPtr` to be
    // `Send` + `Sync`
    #[unsafe_assume_virtual_address_space_independent]
    _phantom: std::marker::PhantomData<SyncSendPointer<T>>,
}

unsafe impl<T> shadow_pod::Pod for ForeignPtr<T> where T: 'static {}

impl<T> ForeignPtr<T> {
    const fn new_with_type_inference(val: usize) -> Self {
        Self {
            val,
            _phantom: std::marker::PhantomData,
        }
    }

    #[inline]
    pub const fn null() -> Self {
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
    #[inline]
    pub const fn cast<U: NoTypeInference>(&self) -> ForeignPtr<U::This> {
        ForeignPtr::new_with_type_inference(self.val)
    }

    #[inline]
    pub const fn is_null(&self) -> bool {
        self.val == 0
    }

    /// Create a `ForeignPtr` from a raw pointer to plugin memory.
    #[inline]
    pub fn from_raw_ptr(ptr: *mut T) -> Self {
        let val = ptr as usize;
        // the type of this `ForeignPtr` will be inferred from the pointer type
        Self::new_with_type_inference(val)
    }

    /// Add an offset to a pointer. `count` is in units of `T`.
    #[inline]
    pub const fn add(&self, count: usize) -> Self {
        let val = self.val;
        Self::new_with_type_inference(val + count * std::mem::size_of::<T>())
    }

    /// Subtract an offset from a pointer. `count` is in units of `T`.
    #[inline]
    pub const fn sub(&self, count: usize) -> Self {
        let val = self.val;
        Self::new_with_type_inference(val - count * std::mem::size_of::<T>())
    }

    /// Convert to a raw pointer. "safe" in itself, but keep in mind that it
    /// isn't dereferenceable if it originated from a different virtual address
    /// space.
    #[inline]
    pub fn into_raw(self) -> *const T {
        self.val as *const T
    }

    /// Convert to a raw pointer. "safe" in itself, but keep in mind that it
    /// isn't dereferenceable if it originated from a different virtual address
    /// space.
    #[inline]
    pub fn into_raw_mut(self) -> *mut T {
        self.val as *mut T
    }
}

impl<T> From<ForeignPtr<T>> for usize {
    #[inline]
    fn from(v: ForeignPtr<T>) -> Self {
        v.val
    }
}

/// Convert an integer to an untyped `ForeignPtr`.
impl From<usize> for ForeignPtr<()> {
    #[inline]
    fn from(v: usize) -> Self {
        ForeignPtr {
            val: v,
            _phantom: Default::default(),
        }
    }
}

/// Convert an integer to an untyped `ForeignPtr`.
impl From<u64> for ForeignPtr<()> {
    #[inline]
    fn from(v: u64) -> Self {
        ForeignPtr {
            val: v.try_into().unwrap(),
            _phantom: Default::default(),
        }
    }
}

impl<T> From<ForeignPtr<T>> for u64 {
    #[inline]
    fn from(v: ForeignPtr<T>) -> Self {
        v.val.try_into().unwrap()
    }
}

impl<T> Copy for ForeignPtr<T> {}

impl<T> Clone for ForeignPtr<T> {
    #[inline]
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
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
#[repr(C)]
pub struct ManagedPhysicalMemoryAddr {
    val: usize,
}

impl From<ManagedPhysicalMemoryAddr> for usize {
    #[inline]
    fn from(v: ManagedPhysicalMemoryAddr) -> usize {
        v.val
    }
}

impl From<usize> for ManagedPhysicalMemoryAddr {
    #[inline]
    fn from(v: usize) -> ManagedPhysicalMemoryAddr {
        ManagedPhysicalMemoryAddr { val: v }
    }
}

impl From<u64> for ManagedPhysicalMemoryAddr {
    #[inline]
    fn from(v: u64) -> ManagedPhysicalMemoryAddr {
        ManagedPhysicalMemoryAddr {
            val: v.try_into().unwrap(),
        }
    }
}

impl From<ManagedPhysicalMemoryAddr> for u64 {
    #[inline]
    fn from(v: ManagedPhysicalMemoryAddr) -> u64 {
        v.val.try_into().unwrap()
    }
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct SyscallArgs {
    // SYS_* from sys/syscall.h.
    // (mostly included from
    // /usr/include/x86_64-linux-gnu/bits/syscall.h)
    pub number: libc::c_long,
    pub args: [SyscallReg; 6],
}

impl SyscallArgs {
    #[inline]
    pub fn get(&self, i: usize) -> SyscallReg {
        self.args[i]
    }
    #[inline]
    pub fn number(&self) -> i64 {
        self.number
    }
}

/// A register used for input/output in a syscall.
#[derive(Copy, Clone, Eq, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub union SyscallReg {
    as_i64: i64,
    as_u64: u64,
    as_ptr: UntypedForeignPtr,
}
// SyscallReg and all of its fields must be transmutable with a 64 bit integer.
// TODO: Store as a single `u64` and explicitly transmute in the conversion
// operations.  This requires getting rid of the direct field access in our C
// code.
static_assertions::assert_eq_align!(SyscallReg, u64);
static_assertions::assert_eq_size!(SyscallReg, u64);

impl PartialEq for SyscallReg {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        unsafe { self.as_u64 == other.as_u64 }
    }
}

impl From<u64> for SyscallReg {
    #[inline]
    fn from(v: u64) -> Self {
        Self { as_u64: v }
    }
}

impl From<SyscallReg> for u64 {
    #[inline]
    fn from(v: SyscallReg) -> u64 {
        unsafe { v.as_u64 }
    }
}

impl From<u32> for SyscallReg {
    #[inline]
    fn from(v: u32) -> Self {
        Self { as_u64: v as u64 }
    }
}

impl From<SyscallReg> for u32 {
    #[inline]
    fn from(v: SyscallReg) -> u32 {
        (unsafe { v.as_u64 }) as u32
    }
}

impl From<usize> for SyscallReg {
    #[inline]
    fn from(v: usize) -> Self {
        Self { as_u64: v as u64 }
    }
}

impl From<SyscallReg> for usize {
    #[inline]
    fn from(v: SyscallReg) -> usize {
        unsafe { v.as_u64 as usize }
    }
}

impl From<isize> for SyscallReg {
    #[inline]
    fn from(v: isize) -> Self {
        Self { as_i64: v as i64 }
    }
}

impl From<SyscallReg> for isize {
    #[inline]
    fn from(v: SyscallReg) -> isize {
        unsafe { v.as_i64 as isize }
    }
}

impl From<i64> for SyscallReg {
    #[inline]
    fn from(v: i64) -> Self {
        Self { as_i64: v }
    }
}

impl From<SyscallReg> for i64 {
    #[inline]
    fn from(v: SyscallReg) -> i64 {
        unsafe { v.as_i64 }
    }
}

impl From<i32> for SyscallReg {
    #[inline]
    fn from(v: i32) -> Self {
        Self { as_i64: v as i64 }
    }
}

impl From<SyscallReg> for i32 {
    #[inline]
    fn from(v: SyscallReg) -> i32 {
        (unsafe { v.as_i64 }) as i32
    }
}

impl TryFrom<SyscallReg> for u8 {
    type Error = <u8 as TryFrom<u64>>::Error;

    #[inline]
    fn try_from(v: SyscallReg) -> Result<u8, Self::Error> {
        (unsafe { v.as_u64 }).try_into()
    }
}

impl TryFrom<SyscallReg> for u16 {
    type Error = <u16 as TryFrom<u64>>::Error;

    #[inline]
    fn try_from(v: SyscallReg) -> Result<u16, Self::Error> {
        (unsafe { v.as_u64 }).try_into()
    }
}

impl TryFrom<SyscallReg> for i8 {
    type Error = <i8 as TryFrom<i64>>::Error;

    #[inline]
    fn try_from(v: SyscallReg) -> Result<i8, Self::Error> {
        (unsafe { v.as_i64 }).try_into()
    }
}

impl TryFrom<SyscallReg> for i16 {
    type Error = <i16 as TryFrom<i64>>::Error;

    #[inline]
    fn try_from(v: SyscallReg) -> Result<i16, Self::Error> {
        (unsafe { v.as_i64 }).try_into()
    }
}

impl<T> From<ForeignPtr<T>> for SyscallReg {
    #[inline]
    fn from(v: ForeignPtr<T>) -> Self {
        Self {
            as_ptr: v.cast::<()>(),
        }
    }
}

impl<T> From<SyscallReg> for ForeignPtr<T> {
    #[inline]
    fn from(v: SyscallReg) -> Self {
        // This allows rust to infer the type for the cast. This isn't ideal since we generally want
        // to require that the user be explicit about casts (for example we use the
        // `NoTypeInference` trait on `ForeignPtr::cast`), but we need this type inference so that
        // `SyscallHandlerFn` can convert the `SyscallReg` to the correct pointer type in syscall
        // handler arguments.
        (unsafe { v.as_ptr }).cast::<T>()
    }
}

// Useful for syscalls whose strongly-typed wrappers return some Result<(), ErrType>
impl From<()> for SyscallReg {
    #[inline]
    fn from(_: ()) -> SyscallReg {
        SyscallReg { as_i64: 0 }
    }
}

impl std::fmt::Debug for SyscallReg {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // prepare the pointer for formatting
        let as_ptr = DebugFormatter(move |fmt| write!(fmt, "{:p}", unsafe { self.as_ptr }));

        f.debug_struct("SyscallReg")
            .field("as_i64", unsafe { &self.as_i64 })
            .field("as_u64", unsafe { &self.as_u64 })
            .field("as_ptr", &as_ptr)
            .finish()
    }
}

// implement conversions from `SyscallReg`

impl From<SyscallReg> for linux_api::sched::CloneFlags {
    fn from(value: SyscallReg) -> Self {
        Self::from_bits_retain(value.into())
    }
}

impl From<SyscallReg> for linux_api::fcntl::OFlag {
    fn from(reg: SyscallReg) -> Self {
        Self::from_bits_retain(reg.into())
    }
}

impl TryFrom<SyscallReg> for nix::sys::eventfd::EfdFlags {
    type Error = ();
    fn try_from(reg: SyscallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SyscallReg> for linux_api::socket::AddressFamily {
    type Error = ();
    fn try_from(reg: SyscallReg) -> Result<Self, Self::Error> {
        Ok(u16::try_from(reg).or(Err(()))?.into())
    }
}

impl TryFrom<SyscallReg> for nix::sys::socket::MsgFlags {
    type Error = ();
    fn try_from(reg: SyscallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl TryFrom<SyscallReg> for nix::sys::stat::Mode {
    type Error = ();
    fn try_from(reg: SyscallReg) -> Result<Self, Self::Error> {
        Self::from_bits(reg.into()).ok_or(())
    }
}

impl From<SyscallReg> for linux_api::mman::ProtFlags {
    fn from(reg: SyscallReg) -> Self {
        Self::from_bits_retain(reg.into())
    }
}

impl From<SyscallReg> for linux_api::mman::MapFlags {
    fn from(reg: SyscallReg) -> Self {
        Self::from_bits_retain(reg.into())
    }
}

impl From<SyscallReg> for linux_api::mman::MRemapFlags {
    fn from(reg: SyscallReg) -> Self {
        Self::from_bits_retain(reg.into())
    }
}

impl From<SyscallReg> for linux_api::close_range::CloseRangeFlags {
    fn from(reg: SyscallReg) -> Self {
        Self::from_bits_retain(reg.into())
    }
}

impl TryFrom<SyscallReg> for linux_api::time::ClockId {
    type Error = ();
    fn try_from(reg: SyscallReg) -> Result<Self, Self::Error> {
        Self::try_from(i32::from(reg)).map_err(|_| ())
    }
}

impl From<SyscallReg> for linux_api::time::ClockNanosleepFlags {
    fn from(reg: SyscallReg) -> Self {
        Self::from_bits_retain(reg.into())
    }
}

impl TryFrom<SyscallReg> for linux_api::time::ITimerId {
    type Error = ();
    fn try_from(reg: SyscallReg) -> Result<Self, Self::Error> {
        Self::try_from(i32::from(reg)).map_err(|_| ())
    }
}

impl From<SyscallReg> for linux_api::prctl::PrctlOp {
    fn from(reg: SyscallReg) -> Self {
        Self::new(reg.into())
    }
}
