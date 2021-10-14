#include <stdlib.h>

// Should ultimately result in death via SIGABRT.  This is different from the
// SIGSEGV test in particular since it *doesn't* go through SIGSEGV's special
// handling to emulate rdstsc.
int main() { abort(); }
