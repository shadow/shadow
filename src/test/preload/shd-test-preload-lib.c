/*
 * gcc -Wall -D _GNU_SOURCE -fPIC -shared -ldl -o test_preload_lib.so test_preload_lib.c
 */

#include <stdio.h>
#include <time.h>
#include <dlfcn.h>

# define RTLD_NEXT  ((void *) -1l)

// define a function pointer that takes a time_t* and returns a time_t, just like the real time function
typedef time_t (*time_fnctptr)(time_t*);
// we will need to store the pointer to the function when we search
static time_fnctptr _time = NULL;

// application variable
static int flag = 0;

void* do_lookup(char* funcname) {
    // clear old error vals
    dlerror();
    // search for symbol
    void* f = dlsym(RTLD_NEXT, funcname);
    // check for error
    char* err = dlerror();
    if(err != NULL) {
        printf("dlsym() error, failed to lookup %s(): '%s'\n", funcname, err);
        return NULL;
    }
    return f;
}

time_t time (time_t *result){
    printf("time wrapper called\n");

    // we need to save pointer to the next time function,
    // which should be libc time
    if( _time == NULL )
    {
        _time = (time_fnctptr)do_lookup("time");
        // check for error
        if(!_time)
        {
            printf("libc_wrapper: failed to load time()\n");
            return -1;
        }
    }

    //Custom application logic - conditionally call libc time()
    if(flag){
        return _time(NULL);
    } else {
        flag = 1;
        return (time_t) -666666;
    }
}

int local_global_func(void) {
    printf("interposed call to local_global_func()\n");
    return 1;
}
