/*
 * gcc -Wall -D _GNU_SOURCE -fPIC -shared -ldl -o test_preload_lib.so test_preload_lib.c
 */

#include <dlfcn.h>
#include <stdio.h>
#include <time.h>

# define RTLD_NEXT  ((void *) -1l)

// define a function pointer that takes a time_t* and returns a time_t, just like the real time function
typedef time_t (*time_fnctptr)(time_t*);
// we will need to store the pointer to the function when we search
static time_fnctptr _time2 = NULL;

// application variable
static int call_next2 = 0;
static time_t default_value2 = (time_t) -888888;

void* do_lookup2(char* funcname) {
    // clear old error vals
    dlerror();
    // search for symbol
    void* f = dlsym(RTLD_NEXT, funcname);
    // check for error
    char* err = dlerror();
    if(err != NULL) {
        fprintf(stdout, "dlsym() error, failed to lookup %s(): '%s'\n", funcname, err);
        return NULL;
    }
    return f;
}

time_t time (time_t *result){
    fprintf(stdout, "time wrapper2 called\n");

    //Custom application logic - conditionally call the dlsym(NEXT) time()
    if(call_next2) {
        // we need to save pointer to the next time function,
        // which should be libc time
        if( _time2 == NULL )
        {
            _time2 = (time_fnctptr)do_lookup2("time");
            // check for error
            if(!_time2)
            {
                fprintf(stdout, "libc_wrapper2: failed to load time()\n");
                return -1;
            }
        }
        return _time2(NULL);
    } else {
        return (time_t) default_value2;
    }
}

void set_call_next2(int should_call_next) {
    fprintf(stdout, "set_call_next wrapper2 called\n");
    call_next2 = should_call_next;
}
