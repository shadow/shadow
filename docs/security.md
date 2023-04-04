# Non-goal: Security

Never run code under Shadow that you wouldn't trust enough to run outside of
Shadow on the same system at the same level of privilege.

While Shadow uses some of the same techniques used by other systems to isolate
potentially vulnerable or malicious software, this is *not* a design goal of
Shadow. A managed program in a Shadow simulation can, if it tries to, detect
that it's running under such a simulation and break out of the "sandbox" to
issue native system calls.

For example:

* Shadow currently doesn't restrict access to the host file
system. A malicious managed program can read and modify the same files that
Shadow itself can.
* Shadow inserts some code via `LD_PRELOAD` into managed processes. This code
intentionally has the ability to make non-interposed system calls (which it uses
to communicate with the Shadow process), and makes no effort to protect itself
from the managed code running in the same process.

## Reporting security issues

Security issues can be reported to
unique\_halberd\_0m@icloud.com
.
