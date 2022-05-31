# Compatibility Notes

## libopenblas

libopenblas is a fairly low-level library, and can get pulled in transitively
via dependencies. e.g., [tgen](https://github.com/shadow/tgen) uses libigraph,
which links against liblapack, which links against blas.

### Deadlocks due to `sched_yield` loops

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

## cURL

### Example

```yaml
general:
  stop_time: 10s
  model_unblocked_syscall_latency: true

network:
  graph:
    type: 1_gbit_switch

hosts:
  server:
    network_node_id: 0
    processes:
    - path: /usr/bin/python3
      args: -m http.server 80
      start_time: 0s
  client:
    network_node_id: 0
    quantity: 3
    processes:
    - path: /usr/bin/curl
      args: -s server
      start_time: 2s
```

```bash
rm -rf shadow.data; shadow shadow.yaml > shadow.log
```

### Notes

1. Older versions of cURL use a busy loop that is incompatible with Shadow and
will cause Shadow to deadlock. Newer versions of cURL, such as the version
provided in Ubuntu 20.04, don't have this issue. See issue #1794 for details. A
workaround for older versions is to use the `model_unblocked_syscall_latency`
option.

## Nginx

### Example

#### `shadow.yaml`

```yaml
general:
  stop_time: 10s

network:
  graph:
    type: 1_gbit_switch

hosts:
  server:
    network_node_id: 0
    processes:
    - path: /usr/sbin/nginx
      args: -c ../../../nginx.conf -p .
      start_time: 0s
  client:
    network_node_id: 0
    quantity: 3
    processes:
    - path: /usr/bin/curl
      args: -s server
      start_time: 2s
```

#### `nginx.conf`

```
error_log stderr;

# shadow wants to run nginx in the foreground
daemon off;

# shadow doesn't support fork()
master_process off;
worker_processes 0;

# don't use the system pid file
pid nginx.pid;

events {
  # we're not using any workers, so this is the maximum number
  # of simultaneous connections we can support
  worker_connections 1024;
}

http {
  include             /etc/nginx/mime.types;
  default_type        application/octet-stream;

  # shadow does not support sendfile()
  sendfile off;

  access_log off;

  server {
    listen 80;

    location / {
      root /var/www/html;
      index index.nginx-debian.html;
    }
  }
}
```

```bash
rm -rf shadow.data; shadow shadow.yaml > shadow.log
```

### Notes

1. Shadow doesn't support `fork()` so you must disable additional processes
using `master_process off` and `worker_processes 0`.

2. Shadow doesn't support `sendfile()` so you must disable it using `sendfile
off`.

## iPerf 2

### Example

```yaml
general:
  stop_time: 10s

network:
  graph:
    type: 1_gbit_switch

hosts:
  server:
    network_node_id: 0
    processes:
    - path: /usr/bin/iperf
      args: -s
      start_time: 0s
  client:
    network_node_id: 0
    processes:
    - path: /usr/bin/iperf
      args: -c server -t 5
      start_time: 2s
```

```bash
rm -rf shadow.data; shadow shadow.yaml > shadow.log
```

### Notes

1. You must use an iPerf 2 version >= `2.1.1`. Older versions of iPerf have a
[no-syscall busy loop][busy-loop] that is incompatible with Shadow.

[busy-loop]: https://sourceforge.net/p/iperf2/code/ci/41bfc67a9d2c654c360953575ee5160ee4d798e7/tree/src/Reporter.c#l506

## iPerf 3

### Example

```yaml
general:
  stop_time: 10s
  model_unblocked_syscall_latency: true

network:
  graph:
    type: 1_gbit_switch

hosts:
  server:
    network_node_id: 0
    processes:
    - path: /bin/iperf3
      args: -s --bind 0.0.0.0
      start_time: 0s
  client:
    network_node_id: 0
    processes:
    - path: /bin/iperf3
      args: -c server -t 5
      start_time: 2s
```

```bash
rm -rf shadow.data; shadow shadow.yaml > shadow.log
```

### Notes

1. By default iPerf 3 servers bind to an IPv6 address, but Shadow doesn't
support IPv6. Instead you need to bind the server to an IPv4 address such as
0.0.0.0.

2. The iPerf 3 server exits with a non-zero error code and the message "unable
to start listener for connections: Address already in use" after the client
disconnects. This is likely due to Shadow not supporting the `SO_REUSEADDR`
socket option.

3. iPerf 3 uses a busy loop that is incompatible with Shadow and will cause
Shadow to deadlock. A workaround is to use the `model_unblocked_syscall_latency`
option.
