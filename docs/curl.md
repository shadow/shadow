# cURL

## Example

```yaml
general:
  stop_time: 10s

experimental:
  unblocked_syscall_limit: 500

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

## Notes

1. Older versions of cURL use a busy loop that is incompatible with Shadow and
will cause Shadow to deadlock. Newer versions of cURL, such as the version
provided in Ubuntu 20.04, don't have this issue. See issue #1794 for details. A
workaround for older versions is to use the experimental
`unblocked_syscall_limit` option.
