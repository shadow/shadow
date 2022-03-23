#include <stdio.h>
#include <stdlib.h>

int main(int argc, const char* argv[]) {
    printf("Hello");
    // We've previously accidentally overridden the `exit` libc function with a
    // bare wrapper around the `exit` syscall.  This is incorrect since the
    // `exit` libc function actually does some cleanup tasks (including flushing
    // open `FILE*` objects, including `stdout`), and then calls the `exit_group`
    // syscall.
    //
    // When that bug is present, this program has no output, since the `printf`
    // above is never flushed.
    exit(EXIT_SUCCESS);
}
