#ifndef SHIM_PATCH_VDSO_H
#define SHIM_PATCH_VDSO_H

// Hot-patch VDSO functions in the current-running programming to call the
// `syscall(2)` function, which can be intercepted via LD_PRELOAD. 
void patch_vdso(void* vdsoBase);

#endif