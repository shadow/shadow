# Compatibility Notes

- [libopenblas](#libopenblas)
- [cURL](#curl)
- [Nginx](#nginx)
- [iPerf 2](#iperf-2)
- [iPerf 3](#iperf-3)
- [etcd (distributed key-value store)](#etcd-distributed-key-value-store)
- [CTorrent and opentracker](#ctorrent-and-opentracker)

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
  [tor-minimal.yaml:109](https://github.com/shadow/shadow/blob/671811339934dca6cefcb43a9343578d85e74a4b/src/test/tor/minimal/tor-minimal.yaml#L109)

See also:

* [libopenblas deadlocks](https://github.com/shadow/shadow/issues/1788)

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
will cause Shadow to deadlock. `model_unblocked_syscall_latency` works around
this (see [busy-loops](limitations.md#busy-loops)). Newer versions of cURL, such as the
version provided in Ubuntu 20.04, don't have this issue. See issue
[#1794](https://github.com/shadow/shadow/issues/1794) for details.

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

1. Shadow doesn't support `fork()` (See [limitations](limitations.md#unimplemented-system-calls-and-options)) so you must disable additional processes
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

## etcd (distributed key-value store)

### Example

Example for etcd version 3.5.x.

```yaml
general:
  stop_time: 30s
  model_unblocked_syscall_latency: true

network:
  graph:
    type: gml
    inline: |
      graph [
        node [
          id 0
          host_bandwidth_down "20 Mbit"
          host_bandwidth_up "20 Mbit"
        ]
        edge [
          source 0
          target 0
          latency "150 ms"
          packet_loss 0.01
        ]
      ]

hosts:
  server1:
    network_node_id: 0
    processes:
    - path: /usr/bin/etcd
      args:
        --name server1
        --log-outputs=stdout
        --initial-cluster-token etcd-cluster-1
        --initial-cluster 'server1=http://server1:2380,server2=http://server2:2380,server3=http://server3:2380'
        --listen-client-urls http://0.0.0.0:2379
        --advertise-client-urls http://server1:2379
        --listen-peer-urls http://0.0.0.0:2380
        --initial-advertise-peer-urls http://server1:2380
    - path: /usr/bin/etcdctl
      args: put my-key my-value
      start_time: 10s
  server2:
    network_node_id: 0
    processes:
    - path: /usr/bin/etcd
      args:
        --name server2
        --log-outputs=stdout
        --initial-cluster-token etcd-cluster-1
        --initial-cluster 'server1=http://server1:2380,server2=http://server2:2380,server3=http://server3:2380'
        --listen-client-urls http://0.0.0.0:2379
        --advertise-client-urls http://server2:2379
        --listen-peer-urls http://0.0.0.0:2380
        --initial-advertise-peer-urls http://server2:2380
    - path: /usr/bin/etcdctl
      args: get my-key
      start_time: 12s
  server3:
    network_node_id: 0
    processes:
    - path: /usr/bin/etcd
      args:
        --name server3
        --log-outputs=stdout
        --initial-cluster-token etcd-cluster-1
        --initial-cluster 'server1=http://server1:2380,server2=http://server2:2380,server3=http://server3:2380'
        --listen-client-urls http://0.0.0.0:2379
        --advertise-client-urls http://server3:2379
        --listen-peer-urls http://0.0.0.0:2380
        --initial-advertise-peer-urls http://server3:2380
    - path: /usr/bin/etcdctl
      args: get my-key
      start_time: 12s
```

```bash
rm -rf shadow.data; shadow shadow.yaml > shadow.log
```

### Notes

1. The etcd binary [must not be statically linked](limitations.md#statically-linked-executables). You can build a dynamically
linked version by replacing `CGO_ENABLED=0` with `CGO_ENABLED=1` in etcd's
`build.sh` script. The etcd packages included in the Debian and Ubuntu APT
repositories are dynamically linked, so they can be used directly.

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
  uploader:
    network_node_id: 0
    processes:
    - path: cp
      args: ../../../foo .
      start_time: 10s
    - path: ctorrent
      args: -t foo -s example.torrent -u http://tracker:6969/announce
      start_time: 11s
    - path: ctorrent
      args: example.torrent
      start_time: 12s
  downloader:
    network_node_id: 0
    quantity: 10
    processes:
    - path: ctorrent
      args: ../uploader/example.torrent
      start_time: 30s
```

```bash
echo "bar" > foo
rm -rf shadow.data; shadow shadow.yaml > shadow.log
cat shadow.data/hosts/downloader1/foo
```

### Notes

1. Shadow must be run as a non-root user since opentracker will attempt to drop
privileges if it detects that the effective user is root.
