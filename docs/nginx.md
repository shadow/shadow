# Nginx

## Example

### `shadow.yaml`

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

### `nginx.conf`

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

## Notes

1. Shadow doesn't support `fork()` so you must disable additional processes
using `master_process off` and `worker_processes 0`.

2. Shadow doesn't support `sendfile()` so you must disable it using `sendfile
off`.
