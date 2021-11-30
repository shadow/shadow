# libopenblas

libopenblas is a fairly low-level library, and can get pulled in transitively
via dependencies. e.g., [tgen](https://github.com/shadow/tgen) uses libigraph,
which links against liblapack, which links against blas.

## Deadlocks due to `sched_yield` loops

libopenblas, when compiled with pthread support, makes extensive use of
spin-waiting in `sched_yield`-loops, which currently result in deadlock under
Shadow.

There are several known workarounds:

* Use a different implementation of libblas. e.g. on Ubuntu, there are several
  alternative packages that can [provide
  libblas](https://packages.ubuntu.com/hirsute/libblas.so.3).  In particular,
  [libblas3](https://packages.ubuntu.com/hirsute/libblas3) doesn't have this issue.

* Install libopenblas compiled without pthread support. e.g. on Ubuntu this can
  be obtained by installing
  [libopenblas0-serial](https://packages.ubuntu.com/hirsute/libopenblas0-serial)
  instead of
  [libopenblas0-pthread](https://packages.ubuntu.com/hirsute/libopenblas0-pthread).

* Configure libopenblas to not use threads at runtime. This can be done by
  setting the environment variable `OPENBLAS_NUM_THREADS=1`, in the process's
  [environment](https://shadow.github.io/docs/guide/shadow_config_spec.html#hostshostnameprocessesenvironment)
  attribute in the Shadow config. Example:
  [tor-minimal.yaml:109](https://github.com/shadow/shadow/blob/671811339934dca6cefcb43a9343578d85e74a4b/src/test/tor/minimal/tor-minimal.yaml#L109)

See also:

* [libopenblas deadlocks](https://github.com/shadow/shadow/issues/1788)
* [sched\_yield loops](https://github.com/shadow/shadow/issues/1792)
