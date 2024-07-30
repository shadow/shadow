# Compatibility Notes

- [libopenblas](#libopenblas)
- [cURL](#curl)
- [Wget2](#wget2)
- [Nginx](#nginx)
- [iPerf 2](#iperf-2)
- [iPerf 3](#iperf-3)
- [Jetty](#jetty)
- [etcd (distributed key-value store)](#etcd-distributed-key-value-store)
- [CTorrent and opentracker](#ctorrent-and-opentracker)
- [http-server](#http-server)

## libopenblas

libopenblas is a fairly low-level library, and can get pulled in transitively
via dependencies. e.g., [tgen](https://github.com/shadow/tgen) uses libigraph,
which links against liblapack, which links against blas.

libopenblas, when compiled with pthread support, uses
[busy-loops](limitations.md#busy-loops) in its worker threads.

There are several known workarounds:

* Use Shadow's `--model-unblocked-syscall-latency` feature. See
  [busy-loops](limitations.md#busy-loops) for details and caveats.

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
  [environment](shadow_config_spec.md#hostshostnameprocessesenvironment)
  attribute in the Shadow config. Example:
  [tor-minimal.yaml:109](https://github.com/shadow/shadow/blob/7ceb8b7793f1e525c7278e1893aa247ad224af76/src/test/tor/minimal/tor-minimal.yaml#L109)

See also:

* [libopenblas deadlocks](https://github.com/shadow/shadow/issues/1788)

## cURL

### Example

```yaml
{{#include ../examples/apps/curl/shadow.yaml}}
```

```bash
{{#include ../examples/apps/curl/run.sh:body}}
```

### Notes

1. Older versions of cURL use a busy loop that is incompatible with Shadow and
will cause Shadow to deadlock. `model_unblocked_syscall_latency` works around
this (see [busy-loops](limitations.md#busy-loops)). Newer versions of cURL, such as the
version provided in Ubuntu 20.04, don't have this issue. See issue
[#1794](https://github.com/shadow/shadow/issues/1794) for details.

## Wget2

### Example

```yaml
{{#include ../examples/apps/wget2/shadow.yaml}}
```

```bash
{{#include ../examples/apps/wget2/run.sh:body}}
```

### Notes

1. Shadow doesn't support `TCP_FASTOPEN` so you must run Wget2 using the `--no-tcp-fastopen` option.

## Nginx

### Example

#### `shadow.yaml`

```yaml
{{#include ../examples/apps/nginx/shadow.yaml}}
```

#### `nginx.conf`

```nginx
{{#include ../examples/apps/nginx/nginx.conf}}
```

```bash
{{#include ../examples/apps/nginx/run.sh:body}}
```

### Notes

1. Shadow currently doesn't support some syscalls that nginx uses to set up and control worker child processes, so you must disable additional processes
using `master_process off` and `worker_processes 0`. See https://github.com/shadow/shadow/issues/3174.

2. Shadow doesn't support `sendfile()` so you must disable it using `sendfile
off`.

## iPerf 2

### Example

```yaml
{{#include ../examples/apps/iperf-2/shadow.yaml}}
```

```bash
{{#include ../examples/apps/iperf-2/run.sh:body}}
```

### Notes

1. You must use an iPerf 2 version >= `2.1.1`. Older versions of iPerf have a
[no-syscall busy loop][busy-loop] that is [incompatible with Shadow](limitations.md#busy-loops).

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
    - path: iperf3
      args: -s --bind 0.0.0.0
      start_time: 0s
      # Tell shadow to expect this process to still be running at the end of the
      # simulation.
      expected_final_state: running
  client:
    network_node_id: 0
    processes:
    - path: iperf3
      args: -c server -t 5
      start_time: 2s
```

```bash
rm -rf shadow.data; shadow shadow.yaml > shadow.log
```

### Notes

1. By default iPerf 3 servers bind to an IPv6 address, but [Shadow doesn't
support IPv6](limitations.md#ipv6). Instead you need to bind the server to an IPv4 address such as
0.0.0.0.

2. The iPerf 3 server exits with a non-zero error code and the message "unable
to start listener for connections: Address already in use" after the client
disconnects. This is likely due to Shadow not supporting the `SO_REUSEADDR`
socket option.

3. iPerf 3 uses a [busy loop](limitations.md#busy-loops) that is incompatible
with Shadow and will cause Shadow to deadlock. A workaround is to use the
`model_unblocked_syscall_latency` option.

## Jetty

Running Jetty with the http module works, but we haven't tested anything more
than this.

### Example

#### `shadow.yaml`

```yaml
{{#include ../examples/apps/jetty/shadow.yaml}}
```

```bash
{{#include ../examples/apps/jetty/run.sh:body}}
```

## etcd (distributed key-value store)

### Example

Example for etcd version 3.3.x.

```yaml
{{#include ../examples/apps/etcd/shadow.yaml}}
```

```bash
{{#include ../examples/apps/etcd/run.sh:body}}
```

### Notes

1. The etcd binary [must not be statically
linked](limitations.md#statically-linked-executables). You can build a
dynamically linked version by replacing `CGO_ENABLED=0` with `CGO_ENABLED=1` in
etcd's `scripts/build.sh` and `scripts/build_lib.sh` scripts. The etcd packages
included in the Debian and Ubuntu APT repositories are dynamically linked, so
they can be used directly.

2. Each etcd peer must be started at a different time since etcd uses the
current time as an RNG seed. See [issue
#2858](https://github.com/shadow/shadow/issues/2858) for details.

3. If using etcd version greater than 3.5.4, you must build etcd from source
and comment out the [keepalive period
assignment](https://github.com/etcd-io/etcd/blob/4485db379e80cc9955c3fdd6a776fc630c32cc36/client/pkg/transport/keepalive_listener.go#L68-L70)
as Shadow does not support this.

## CTorrent and opentracker

### Example

```yaml
general:
  stop_time: 60s

network:
  graph:
    type: 1_gbit_switch

hosts:
  tracker:
    network_node_id: 0
    processes:
    - path: opentracker
      # Tell shadow to expect this process to still be running at the end of the
      # simulation.
      expected_final_state: running
  uploader:
    network_node_id: 0
    processes:
    - path: cp
      args: ../../../foo .
      start_time: 10s
    # Create the torrent file
    - path: ctorrent
      args: -t foo -s example.torrent -u http://tracker:6969/announce
      start_time: 11s
    # Serve the torrent
    - path: ctorrent
      args: example.torrent
      start_time: 12s
      expected_final_state: running
  downloader1: &downloader_host
    network_node_id: 0
    processes:
    # Download and share the torrent
    - path: ctorrent
      args: ../uploader/example.torrent
      start_time: 30s
      expected_final_state: running
  downloader2: *downloader_host
  downloader3: *downloader_host
  downloader4: *downloader_host
  downloader5: *downloader_host
```

```bash
echo "bar" > foo
rm -rf shadow.data; shadow shadow.yaml > shadow.log
cat shadow.data/hosts/downloader1/foo
```

### Notes

1. Shadow must be run as a non-root user since opentracker will attempt to drop
privileges if it detects that the effective user is root.

## http-server

### Example

```yaml
{{#include ../examples/apps/http-server/shadow.yaml}}
```

```bash
{{#include ../examples/apps/http-server/run.sh:body}}
```

### Notes

1. Either the Node.js runtime or http-server uses a busy loop that is
incompatible with Shadow and will cause Shadow to deadlock.
`model_unblocked_syscall_latency` works around this (see
[busy-loops](limitations.md#busy-loops)).
