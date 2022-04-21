# iPerf 3

## Example

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

## Notes

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
