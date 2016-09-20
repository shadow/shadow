#ifndef DPRINTF_H
#define DPRINTF_H

/**
 * This function uses no global variable and is thus fairly safe
 * to call from any situation.
 */
void dprintf (const char *str, ...);

#ifdef DPRINTF_DEBUG_ENABLE
#define DPRINTF(str,...) \
  dprintf(str, __VA_ARGS__)
#else
#define DPRINTF(str,...)
#endif

#endif /* DPRINTF_H */
