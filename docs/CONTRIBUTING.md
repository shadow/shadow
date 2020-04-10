## Coding style

### Clang-format

Our code formatting style is defined in our
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

### Including headers

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
