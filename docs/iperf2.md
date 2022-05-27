# iPerf 2

## Example

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

## Notes

1. You must use an iPerf 2 version >= `2.1.1`. Older versions of iPerf have a
[no-syscall busy loop][busy-loop] that is incompatible with Shadow.

[busy-loop]: https://sourceforge.net/p/iperf2/code/ci/41bfc67a9d2c654c360953575ee5160ee4d798e7/tree/src/Reporter.c#l506
