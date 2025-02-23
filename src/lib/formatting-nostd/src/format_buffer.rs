use core::ffi::CStr;
use core::mem::MaybeUninit;

/// A self-contained buffer that can be used with both Rust's formatting utilities and
/// libc's sprintf.
///
/// Because those tools panic on errors, overflowing writes are truncated rather
/// than returning an error. A non-zero truncation count is included in
/// `Display` output of this object, and can be checked via the `truncated`
/// method.
///
/// The generic parameter `N` is the internal size of the buffer.  One byte is
/// reserved for NULL to support conversion to `CStr`.
///
/// To format a message with Rust's formatting:
/// ```
/// # use formatting_nostd::FormatBuffer;
/// use core::fmt::Write;
/// let mut buf = FormatBuffer::<1000>::new();
/// let x = 42;
/// write!(&mut buf, "{x}").unwrap();
/// assert_eq!(buf.as_str(), "42");
/// let y = 43;
/// write!(&mut buf, " {y}").unwrap();
/// assert_eq!(buf.as_str(), "42 43");
/// ```
pub struct FormatBuffer<const N: usize> {
    buffer: [MaybeUninit<u8>; N],
    /// Does *not* include NULL byte.
    used: usize,
    truncated: usize,
}

impl<const N: usize> FormatBuffer<N> {
    const CAPACITY_INCLUDING_NULL: usize = N;
    const CAPACITY: usize = N - 1;

    pub fn new() -> Self {
        assert!(Self::CAPACITY_INCLUDING_NULL >= 1);
        let mut res = Self {
            buffer: [MaybeUninit::uninit(); N],
            used: 0,
            truncated: 0,
        };
        res.null_terminate();
        res
    }

    /// Remaining capacity in bytes.
    pub fn capacity_remaining(&self) -> usize {
        Self::CAPACITY - self.used
    }

    pub fn capacity_remaining_including_null(&self) -> usize {
        Self::CAPACITY_INCLUDING_NULL - self.used
    }

    /// How many bytes (not chars) have been truncated.
    /// This shouldn't be relied on for an exact count; in particular
    /// the accounting is not precise in `sprintf` if utf8 replacement
    /// characters need to be inserted.
    pub fn truncated(&self) -> usize {
        self.truncated
    }

    fn null_terminate(&mut self) {
        self.buffer[self.used].write(0);
    }

    /// Reset to empty. This may be cheaper than assigning a fresh
    /// `FormatBuffer::new`, since the latter requires copying the uninitialized
    /// buffer. (Though such a copy could get optimized to the same cost
    /// depending on opt level, inlining, etc.)
    pub fn reset(&mut self) {
        self.used = 0;
        self.truncated = 0;
        self.null_terminate();
    }

    // The initialized part of the internal buffer.
    fn initd_buffer_including_null(&self) -> &[u8] {
        let buffer: *const MaybeUninit<u8> = self.buffer.as_ptr();
        // MaybeUninit<u8> is guaranteed to have the same ABI as u8.
        let buffer: *const u8 = buffer as *const u8;
        // SAFETY: We know this byte range is initialized.
        let rv = unsafe { core::slice::from_raw_parts(buffer, self.used + 1) };
        assert_eq!(rv.last(), Some(&0));
        rv
    }

    fn initd_buffer_excluding_null(&self) -> &[u8] {
        let res = self.initd_buffer_including_null();
        &res[..(res.len() - 1)]
    }

    /// `str` representation of internal buffer.
    ///
    /// If you'd like to render the buffer including any non-zero
    /// truncation count, use the `Display` attribute instead.
    pub fn as_str(&self) -> &str {
        // SAFETY: We've ensured that only valid utf8 is appended to the buffer.
        unsafe { core::str::from_utf8_unchecked(self.initd_buffer_excluding_null()) }
    }

    /// Returns `None` if the buffer has interior NULL bytes.
    pub fn as_cstr(&self) -> Option<&CStr> {
        CStr::from_bytes_with_nul(self.initd_buffer_including_null()).ok()
    }

    /// Appends the result of formatting `fmt` and `args`, following the conventions
    /// of libc's `sprintf`.
    ///
    /// Any non-utf8 sequences in the resulting string are replaced with the
    /// utf8 replacement character. If truncation occurs, the truncation count
    /// doesn't necessarily account for all such substitutions.
    ///
    /// Currently calls libc's `vsnprintf` internally and panics on unexpected error.
    /// TODO: Ideally we'd find or create our own reimplementation of `vsnprintf` instead,
    /// since `vsnprintf` isn't guaranteed to be async-signal-safe.
    ///
    /// # Safety
    ///
    /// `fmt` and `args` must be consistent, as with arguments to libc's `sprintf`.
    pub unsafe fn sprintf(&mut self, fmt: &CStr, args: va_list::VaList) {
        // We use a temp buffer for the direct libc destination, so that we
        // can relatively easily do a lossy utf8 decode from that buffer to
        // our internal buffer.
        //
        // We *could* instead do a lossy decode in place to avoid having to
        // allocate this additional buffer. However, because the unicode
        // replacement character is multiple bytes, each insertion would be an
        // O(N) to shift of the rest of the buffer.  Performance-wise that's
        // probably fine since in the common case nothing would be substituted,
        // but it'd also make the code significantly trickier.
        //
        // Meanwhile, this stack allocation is ~free... as long as we don't
        // overflow the stack.
        let mut buf = [MaybeUninit::<i8>::uninit(); N];

        let rv = unsafe { vsnprintf(buf.as_mut_ptr() as *mut i8, buf.len(), fmt.as_ptr(), args) };

        // Number of non-NULL bytes for the fully formatted string.
        let formatted_len = match usize::try_from(rv) {
            Ok(n) => n,
            Err(_) => {
                panic!("vsnprintf returned {rv}");
            }
        };

        // we use a hyper-local helper function to ensure that the new slice has the correct lifetime.
        // <https://doc.rust-lang.org/std/slice/fn.from_raw_parts.html#caveat>
        unsafe fn transmute_to_u8(buf: &[MaybeUninit<i8>]) -> &[u8] {
            unsafe { core::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len()) }
        }

        // `vsnprintf` never writes more bytes than the size of the buffer, and
        // always NULL-terminates.  i.e. if it had to truncate, then only
        // `buf.len()-1` non-NULL bytes will have been written.
        let non_null_bytes_written = core::cmp::min(buf.len() - 1, formatted_len);
        let initd_buf = unsafe { transmute_to_u8(&buf[..non_null_bytes_written]) };

        for decoded_char in crate::utf8::decode_lossy(initd_buf) {
            if self.truncated > 0 || decoded_char.len() > self.capacity_remaining() {
                self.truncated += decoded_char.len()
            } else {
                self.write_fitting_str(decoded_char)
            }
        }

        // Also account for bytes truncated in our call to vsnprintf. We do this
        // *after* the decoding loop to support writing as much as we can of the
        // current vsnprintf result before we start truncating.
        self.truncated += formatted_len - non_null_bytes_written;
        self.null_terminate();
    }

    // Panics if the bytes don't fit.
    fn write_fitting_str(&mut self, src: &str) {
        assert!(src.len() <= self.capacity_remaining());

        // SAFETY: the pointer arithmetic here stays inside the original object (the buffer).
        let dst: *mut MaybeUninit<u8> = unsafe { self.buffer.as_mut_ptr().add(self.used) };

        // `MaybeUninit` guarantees this cast is safe, as long as we don't try to read
        // the uninitialized data.
        let dst: *mut u8 = dst as *mut u8;

        unsafe { core::ptr::copy_nonoverlapping(src.as_ptr(), dst, src.len()) };
        self.used += src.len();
        self.null_terminate();
    }
}

impl<const N: usize> core::fmt::Write for FormatBuffer<N> {
    fn write_str(&mut self, src: &str) -> Result<(), core::fmt::Error> {
        if self.truncated() > 0 {
            // Never write more after having started truncating.
            self.truncated += src.len();
            return Ok(());
        }

        if src.len() <= self.capacity_remaining() {
            self.write_fitting_str(src);
            return Ok(());
        }

        // Find safe end to split at.
        // TODO: consider `str::floor_char_boundary` once it's stabilized.
        let mut nbytes = self.capacity_remaining();
        while !src.is_char_boundary(nbytes) {
            nbytes -= 1;
        }
        self.truncated += src.len() - nbytes;

        self.write_fitting_str(&src[..nbytes]);
        Ok(())
    }
}

impl<const N: usize> core::fmt::Display for FormatBuffer<N> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        if self.truncated == 0 {
            write!(f, "{}", self.as_str())
        } else {
            write!(f, "{}...<truncated {}>", self.as_str(), self.truncated())
        }
    }
}

impl<const N: usize> Default for FormatBuffer<N> {
    fn default() -> Self {
        Self::new()
    }
}

// Ensure the system libc is linked.
extern crate libc;

unsafe extern "C" {
    // Use libc's `vsnprintf` function. The `libc` crate doesn't expose it, so
    // we declare it ourselves.
    //
    // From `sprintf(3)`:
    // > int vsnprintf(char *str, size_t size, const char *format, va_list ap);
    //
    // `va_list::VaList` is ABI compatible with libc's `va_list`.
    fn vsnprintf(
        str: *mut core::ffi::c_char,
        size: usize,
        fmt: *const core::ffi::c_char,
        ap: va_list::VaList,
    ) -> i32;
}

#[cfg(test)]
mod test {
    use core::fmt::Write;

    use std::ffi::CString;

    use super::*;

    #[test]
    fn test_format_buffer_write_str_exact() {
        let mut buf = FormatBuffer::<4>::new();
        assert!(buf.write_str("123").is_ok());
        assert_eq!(buf.as_str(), "123");
        assert_eq!(buf.truncated(), 0);
    }

    #[test]
    fn test_format_buffer_write_str_truncated() {
        let mut buf = FormatBuffer::<3>::new();
        assert!(buf.write_str("123").is_ok());
        assert_eq!(buf.as_str(), "12");
        assert_eq!(buf.truncated(), 1);
    }

    #[test]
    fn test_format_buffer_write_str_truncated_unicode() {
        let mut buf = FormatBuffer::<3>::new();
        // U+00A1 "inverted exclamation mark" is 2 bytes in utf8.
        // Ensure that both bytes are truncated, rather than splitting in the
        // middle.
        assert!(buf.write_str("1¡").is_ok());
        assert_eq!(buf.as_str(), "1");
        assert_eq!(buf.truncated(), 2);

        // While there is 1 byte of capacity left, once bytes have been truncated
        // the buffer truncates all additional writes.
        assert_eq!(buf.capacity_remaining(), 1);
        assert!(buf.write_str("2").is_ok());
        assert_eq!(buf.capacity_remaining(), 1);
        assert_eq!(buf.truncated(), 3);
    }

    #[test]
    fn test_format_buffer_display_truncated() {
        let mut buf = FormatBuffer::<3>::new();
        assert!(buf.write_str("123").is_ok());
        assert_eq!(format!("{buf}"), "12...<truncated 1>");
    }

    #[test]
    fn test_format_buffer_write_str_multiple() {
        let mut buf = FormatBuffer::<7>::new();
        assert!(buf.write_str("123").is_ok());
        assert_eq!(buf.as_str(), "123");
        assert!(buf.write_str("456").is_ok());
        assert_eq!(buf.as_str(), "123456");
    }

    #[test]
    fn test_cstr_ok() {
        let mut buf = FormatBuffer::<7>::new();
        assert!(buf.write_str("123").is_ok());
        let expected = CString::new("123").unwrap();
        assert_eq!(buf.as_cstr(), Some(expected.as_c_str()));
    }
}

// sprintf tests don't work under miri since we use FFI.
#[cfg(all(test, not(miri)))]
mod sprintf_test {
    use std::ffi::CString;

    use super::*;

    // Wrapper code we expose to our C test harness.
    #[unsafe(no_mangle)]
    unsafe extern "C-unwind" fn test_format_buffer_valist(
        format_buffer: *mut FormatBuffer<10>,
        fmt: *const core::ffi::c_char,
        args: va_list::VaList,
    ) {
        let fmt = unsafe { CStr::from_ptr(fmt) };
        let format_buffer = unsafe { format_buffer.as_mut().unwrap() };
        unsafe { format_buffer.sprintf(fmt, args) };
    }

    unsafe extern "C-unwind" {
        // Wrapper code that our C test harness exposes to us.
        // It calls `test_format_buffer_valist` and returns the result.
        //
        // We need this to transform varargs (...) to a `VaList`;
        // we don't have a way to construct a `VaList` in pure Rust.
        #[allow(improper_ctypes)]
        fn test_format_buffer_vararg(
            format_buffer: *mut FormatBuffer<10>,
            fmt: *const core::ffi::c_char,
            ...
        );
    }

    #[test]
    fn test_sprintf_noargs() {
        let mut buf = FormatBuffer::<10>::new();
        let fmt = CString::new("hello").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr()) };
        assert_eq!(buf.as_str(), "hello");
        assert_eq!(buf.truncated(), 0);
    }

    #[test]
    fn test_sprintf_args() {
        let mut buf = FormatBuffer::<10>::new();
        let fmt = CString::new("x %d y").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr(), 42i32) };
        assert_eq!(buf.as_str(), "x 42 y");
        assert_eq!(buf.truncated(), 0);
    }

    #[test]
    fn test_sprintf_truncated() {
        let mut buf = FormatBuffer::<10>::new();
        let fmt = CString::new("1234567890123").unwrap();

        // The last *4* bytes will be truncated, only writing *9*.
        // Internally we use libc's `vsnprintf` which always NULL-terminates.
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr()) };
        assert_eq!(buf.as_str(), "123456789");
        assert_eq!(buf.truncated(), 4);
    }

    #[test]
    fn test_sprintf_truncated_partly_full() {
        let mut buf = FormatBuffer::<10>::new();
        let fmt = CString::new("12345678").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr()) };
        assert_eq!(buf.as_str(), "12345678");
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr()) };
        assert_eq!(buf.as_str(), "123456781");
        assert_eq!(buf.truncated(), 7);
    }

    #[test]
    fn test_sprintf_truncated_unicode() {
        let mut buf = FormatBuffer::<10>::new();
        // U+00A1 "inverted exclamation mark" is 2 bytes in utf8.
        // Ensure that both bytes are truncated, rather than splitting in the
        // middle.
        let fmt = CString::new("123456789¡").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr()) };
        assert_eq!(buf.as_str(), "123456789");
        assert_eq!(buf.truncated(), 2);
    }

    #[test]
    fn test_sprintf_unicode_replacement() {
        let mut buf = FormatBuffer::<10>::new();
        // Cause the formatted output to have a continuation byte 0x80 without
        // a previous start byte; i.e. be invalid utf8. It should get replaced with
        // a replacment character.
        let fmt = CString::new("x%cy").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr(), 0x80 as core::ffi::c_int) };
        assert_eq!(buf.as_str(), "x�y");
        assert_eq!(buf.truncated(), 0);
    }

    #[test]
    fn test_sprintf_unicode_replacement_truncation() {
        let mut buf = FormatBuffer::<10>::new();
        // Cause the formatted output to have a continuation byte 0x80 without
        // a previous start byte; i.e. be invalid utf8. It should get replaced with
        // a replacment character.
        let fmt = CString::new("12345678%c").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr(), 0x80 as core::ffi::c_int) };
        // The unicode replacement charater won't fit, so should get truncated completely.
        assert_eq!(buf.as_str(), "12345678");
        // We're not guaranteeing anything about the exact count in this case,
        // other than it should be non-zero.
        assert!(buf.truncated() > 0);
    }

    #[test]
    fn test_sprintf_multiple() {
        let mut buf = FormatBuffer::<10>::new();
        let fmt = CString::new("123").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr()) };
        let fmt = CString::new("456").unwrap();
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr()) };
        assert_eq!(buf.as_str(), "123456");
        assert_eq!(buf.truncated(), 0);
    }

    #[test]
    fn test_sprintf_cstr_fail() {
        let mut buf = FormatBuffer::<10>::new();
        // Cause the formatted output to have an interior NULL byte.
        let fmt = CString::new("1234%c56").unwrap();

        // We have to cast 0 to `c_int` here, because the vararg ABI doesn't
        // support passing a char. (i.e. casting to `c_char` fails to compile)
        unsafe { test_format_buffer_vararg(&mut buf, fmt.as_ptr(), 0 as core::ffi::c_int) };
        assert_eq!(buf.as_cstr(), None);
    }
}
