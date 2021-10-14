// Dieing with SIGSEGV in particular is a special case since we also use
// intercept rdtsc as SIGSEGV, via `prctl(PR_SET_TSC)`.
int main() {
    // Access memory address 0, triggering a SEGV. Shadow should detect that the
    // process has exited and clean it up.
    //
    // We use assembly here to ensure we really access the NULL pointer and get a
    // SEGV. Done at the C level, the compiler could detect undefined behavior
    // and do something else.
    asm("mov 0, %rax");
}
