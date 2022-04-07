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
    - path: iperf2-code/src/iperf
      args: -s
      start_time: 0s
  client:
    network_node_id: 0
    processes:
    - path: iperf2-code/src/iperf
      args: -c server -t 5
      start_time: 2s
```

```bash
rm -rf shadow.data; shadow shadow.yaml > shadow.log
```

## Notes

1. Shadow doesn't support `setiterm()` so you must build iPerf 2 with `#define
HAVE_SETITIMER 0` set.
