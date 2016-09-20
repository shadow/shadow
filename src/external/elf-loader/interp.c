// We have to set the interpreter name with this hack
// because the linker will ignore --dynamic-linker=ldso
// when building a shared library and will thus
// refuse to output a PT_INTERP and PT_PHDR entry
// in the program header table.
const char __invoke_dynamic_linker__[] __attribute__ ((section (".interp"))) = "ldso";

