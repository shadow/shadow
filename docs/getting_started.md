# Getting Started

When installing Shadow, the main executable was placed in `/bin` in your install prefix (`~/.local/bin` by default). As a reminder, it would be helpful if this location was included in your environment `PATH`.

The main Shadow binary executable, `shadow`, contains most of the simulator's code, including events and the event engine, the network stack, and the routing logic. Shadow's event engine supports multi-threading using the `-p` or `--parallelism` flags (or their corresponding [configuration file option](shadow_config_options.md#generalparallelism)) to simulate multiple hosts in parallel.

Shadow can typically run applications without modification, but there are a few limitations to be aware of:

 - Not all system calls are supported yet. Notable unsupported syscalls include fork and exec.
 - Applications should not use or expect signals.
 - Shadow does not support IPv6.

## Example HTTP Server

The following example simulates the network traffic of an HTTP server with 3 clients, each running on different virtual hosts. If you do not have Python or cURL installed, you can download them through your distribution's package manager.

Each client uses cURL to make an HTTP request to a basic Python HTTP server.

`server.py`:

```python
import http.server

httpd = http.server.HTTPServer(('', 80), http.server.SimpleHTTPRequestHandler)
httpd.serve_forever()
```

Shadow requires a configuration file that specifies information about the network topology and the processes to run within the simulation. This example uses a built-in network graph for simplicity. Write this configuration file to the same directory as the `server.py` Python script above.

`shadow.yaml`:

```yaml
general:
  # stop after 10 simulated seconds
  stop_time: 10s

network:
  graph:
    # use a built-in network graph containing
    # a single vertex with a bandwidth of 1 Gbit
    type: 1_gbit_switch

hosts:
  # a host with the hostname 'server'
  server:
    processes:
    - path: /bin/python3
      args: ../../../server.py
      start_time: 3s
  # three hosts with hostnames 'client1', 'client2', and 'client3'
  client:
    quantity: 3
    processes:
    - path: /bin/curl
      args: -s server
      start_time: 5s
```

Shadow stores simulation data to the `shadow.data` directory by default. We first remove this directory if it already exists, and then run Shadow.

```bash
# delete any existing simulation data
rm -rf shadow.data
shadow shadow.yaml > shadow.log
```

This small Shadow simulation should complete almost immediately.

### Simulation Output

Shadow will write simulation output to the data directory (in this example we'll assume the default directory of `shadow.data`). Each host has its own directory under `shadow.data/hosts`. For example:

```bash
$ ls -l shadow.data/hosts
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client1
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client2
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client3
drwxrwxr-x 2 user user 4096 Jun  2 16:54 server
```

Each host directory contains the output for each process running on that host. For example:

```bash
$ ls -l shadow.data/hosts/client1
-rw-rw-r-- 1 user user   1 Jun  2 16:54 client1.curl.1000.exitcode
-rw-rw-r-- 1 user user   0 Jun  2 16:54 client1.curl.1000.shimlog
-rw-r--r-- 1 user user   0 Jun  2 16:54 client1.curl.1000.stderr
-rw-r--r-- 1 user user 542 Jun  2 16:54 client1.curl.1000.stdout

$ cat shadow.data/hosts/client1/client1.curl.1000.stdout
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<title>Directory listing for /</title>
</head>
<body>
<h1>Directory listing for /</h1>
...
```

Each host directory is also the [working directory](https://en.wikipedia.org/wiki/Working_directory) for the host's processes, which is why we specified `../../../server.py` as the path to the Python script in our Shadow configuration file (`./shadow.data/hosts/server/../../../server.py` → `./server.py`).
