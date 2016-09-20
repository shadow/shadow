#ifndef MACROS_H
#define MACROS_H

#define EXPORT __attribute__ ((visibility("default")))
#define RETURN_ADDRESS ((unsigned long)__builtin_return_address (0))
#define MACROS_USED __attribute__ ((used))

#endif /* MACROS_H */
