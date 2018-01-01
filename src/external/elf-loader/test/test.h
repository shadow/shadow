#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#define LIB(name)                                               \
  static __attribute__ ((constructor))                          \
  void constructor (void)                                       \
  {                                                             \
    printf ("lib%s constructor\n", #name);                      \
  }                                                             \
  static __attribute__ ((destructor))                           \
  void destructor (void)                                        \
  {                                                             \
    printf ("lib%s destructor\n", #name);                       \
  }                                                             \
  void __attribute__ ((noinline)) function_##name (void)        \
  {                                                             \
    printf ("called function_%s in lib%s\n",                    \
            #name, #name);                                      \
  }                                                             \
  void __attribute__ ((noinline)) call_function_##name (void)   \
  {                                                             \
    printf ("calling function_%s in lib%s\n",                   \
            #name, #name);                                      \
    function_##name ();                                         \
  }
#endif
