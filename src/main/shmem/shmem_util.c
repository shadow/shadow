#include "shmem_util.h"

/*
logarithm code adopted from:
<https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogLookup>

* License Agreement *

``Individually, the code snippets here are in the public domain (unless
otherwise noted) — feel free to use them however you please. The aggregate
collection and descriptions are © 1997-2005 Sean Eron Anderson. The code and
descriptions are distributed in the hope that they will be useful, but WITHOUT
ANY WARRANTY and without even the implied warranty of merchantability or fitness
for a particular purpose. As of May 5, 2005, all the code has been tested
thoroughly. Thousands of people have read it. Moreover, Professor Randal Bryant,
the Dean of Computer Science at Carnegie Mellon University, has personally
tested almost everything with his Uclid code verification system. What he hasn't
tested, I have checked against all possible inputs on a 32-bit machine. To the
first person to inform me of a legitimate bug in the code, I'll pay a bounty of
US$10 (by check or Paypal). If directed to a charity, I'll pay US$20.''
*/

static const char _log_table_256[256] = {
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
    -1,    0,     1,     1,     2,     2,     2,     2,     3,     3,     3,
    3,     3,     3,     3,     3,     LT(4), LT(5), LT(5), LT(6), LT(6), LT(6),
    LT(6), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7)};

uint32_t shmem_util_uintLog2(uint32_t v) {
    uint32_t r;              // r will be lg(v)
    register uint32_t t, tt; // temporaries

    if ((tt = v >> 16)) {
        r = ((t = tt >> 8)) ? 24 + _log_table_256[t] : 16 + _log_table_256[tt];
    } else {
        r = ((t = v >> 8)) ? 8 + _log_table_256[t] : _log_table_256[v];
    }

    return r;
}
