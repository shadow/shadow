# Coding style

## Logging

In Rust code, we use the [log](https://docs.rs/log/latest/log/) framework for
logging. In C code we use a wrapper library that *also* uses Rust's
[log](https://docs.rs/log/latest/log/) framework internally.

For general guidance on what levels to log at, see [log::Level](https://docs.rs/log/latest/log/enum.Level.html#variants).

Some shadow-specific log level policies:

* We reserve the `Error` level for situations in which the `shadow`
process as a whole will exit with a non-zero code. Conversely, when `shadow` exits
with a non-zero code, the user should be able to get some idea of what caused it by
looking at the `Error`-level log entries.

* `Warning` should be used for messages that ought to be checked by the user
before trusting the results of a simulation. For example, we use these in
syscall handlers when an unimplemented syscall or option is used, and shadow is
forced to return something like `ENOTSUP`, `EINVAL` or `ENOSYS`. In such cases
the simulation is able to continue, and *might* still be representative of what
would happen on a real Linux system; e.g. libc will often fall back to an older
syscall, resulting in minimal impact on the simulated behavior of the managed
process.

## Clang-format

Our C code formatting style is defined in our
[clang-format](https://clang.llvm.org/docs/ClangFormat.html) [configuration
file](../.clang-format). We try to avoid mass re-formatting, but generally any
lines you modify should be reformatted using `clang-format`.

To add Ctrl-k as a "format region" in visual and select modes of vim, add the
following to your .vimrc:

```
vmap <C-K> :py3f /usr/share/vim/addons/syntax/clang-format.py<cr>
```

Alternatively you can use the
[git-clang-format](https://github.com/llvm-mirror/clang/blob/master/tools/clang-format/git-clang-format)
tool on the command-line to modify the lines touched by your commits.

### Rustfmt

To format your Rust code, run `cargo fmt` in the `src` directory.

```bash
(cd src && cargo fmt)
```

### Clippy

We use [Clippy](https://doc.rust-lang.org/stable/clippy/index.html) to help
detect errors and non-idiomatic Rust code. You can run `clippy` locally with:

```bash
(cd src && cargo clippy --all-targets)
```

## Including headers

### Which headers to include

Every source and header file should directly include the headers that export
all referenced symbols and macros.

In a C file, includes should be broken into blocks, with the includes sorted
alphabetically within each block. The blocks should occur in this order:

 * The C file's corresponding header; e.g. `foo.h` for `foo.c`. This enforces
   that the header is self-contained; i.e. doesn't depend on other headers to
   be included before it.
 * System headers are included next to minimize unintentionally exposing any
   macros we define to them.
 * Any other necessary internal headers.

This style is loosely based on that used in
[glib](https://wiki.gnome.org/Projects/GTK/BestPractices/GlibIncludes) and
supported by the [include what you use](https://include-what-you-use.org/)
tool.

### Inclusion style

Headers included from within the project should use quote-includes, and should
use paths relative to `src/`. e.g. `#include "main/utility/byte_queue.h"`, not
`#include "byte_queue.h"` (even from within the same directory), and not
`#include <main/utility/byte_queue.h>`.

Headers included external to this repository should use angle-bracket includes.
e.g. `#include <glib.h>`, not `#include "glib.h"`.
