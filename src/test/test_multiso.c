#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    void * ref1;
    void * ref2;
    void (*testfunc1)(void);
    void (*testfunc2)(void);

    ref1 = dlopen("./test_multiso_lib.so",RTLD_LAZY|RTLD_DEEPBIND);
    if(!ref1) {
        perror("unable to open lib 1 ");
        return 1;
    }

    ref2 = dlopen("./test_multiso_lib2.so",RTLD_LAZY|RTLD_DEEPBIND);
    if(!ref2) {
        perror("unable to open lib 2 ");
        return 1;
    }

    testfunc1 = dlsym(ref1, "test_function");
    testfunc2 = dlsym(ref2, "test_function");

    if(!testfunc1 || !testfunc2) {
        perror("unable to resolve 'test_function'");
        return 1;
    }

    (*testfunc1)();
    (*testfunc2)();
    
    dlclose(ref1);
    dlclose(ref2);

    return 0;
 }
