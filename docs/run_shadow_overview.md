# Running Shadow Overview

When installing Shadow, the main executable was placed in `/bin` in your install
prefix (`~/.local/bin` by default). As a reminder, it would be helpful if this
location was included in your environment `PATH`.

The main Shadow binary executable, `shadow`, contains most of the simulator's
code, including events and the event engine, the network stack, and the routing
logic. Shadow's event engine supports multi-threading using the `-p` or
`--parallelism` flags (or their corresponding [configuration file
option](shadow_config_spec.md#generalparallelism)) to simulate multiple hosts
in parallel.

Shadow can typically run applications without modification, but there are a few
limitations to be aware of:

 - Not all system calls are supported yet. Notable unsupported syscalls include
   fork and exec.
 - Applications should not use or expect signals.
 - Shadow does not support IPv6.
