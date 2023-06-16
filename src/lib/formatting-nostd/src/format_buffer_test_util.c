#include <stdarg.h>
#include <stddef.h>

// Manual declaration of implementation defined in Rust.
// Doesn't seem worth bringing in bindgen for one test function.
void test_format_buffer_valist(void* format_buffer, const char* fmt, va_list args);

void test_format_buffer_vararg(void* format_buffer, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    test_format_buffer_valist(format_buffer, fmt, args);
    va_end(args);
}