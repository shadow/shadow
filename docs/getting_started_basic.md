# Basic File Transfer Example

Here we present a basic example that simulates the network traffic of an HTTP
server with 3 clients, each running on different virtual hosts. If you do not
have Python or cURL installed, you can download them through your distribution's
package manager.

## Configuring the Simulation

Each client uses cURL to make an HTTP request to a basic Python HTTP server.

Shadow requires a configuration file that specifies information about the
network graph and the processes to run within the simulation. This example
uses a built-in network graph for simplicity.

`shadow.yaml`:

```yaml
general:
  # stop after 10 simulated seconds
  stop_time: 10s
  # old versions of cURL use a busy loop, so to avoid spinning in this busy
  # loop indefinitely, we add a system call latency to advance the simulated
  # time when running non-blocking system calls
  model_unblocked_syscall_latency: true

network:
  graph:
    # use a built-in network graph containing
    # a single vertex with a bandwidth of 1 Gbit
    type: 1_gbit_switch

hosts:
  # a host with the hostname 'server'
  server:
    network_node_id: 0
    processes:
    - path: /usr/bin/python3
      args: -m http.server 80
      start_time: 3s
  # three hosts with hostnames 'client1', 'client2', and 'client3'
  client:
    network_node_id: 0
    quantity: 3
    processes:
    - path: /usr/bin/curl
      args: -s server
      start_time: 5s
```

## Running the Simulation

Shadow stores simulation data to the `shadow.data/` directory by default. We
first remove this directory if it already exists, and then run Shadow.

```bash
# delete any existing simulation data
rm -rf shadow.data/
shadow shadow.yaml > shadow.log
```

This small Shadow simulation should complete almost immediately.

## Viewing the Simulation Output

Shadow will write simulation output to the data directory `shadow.data/`. Each
host has its own directory under `shadow.data/hosts/`. For example:

```bash
$ ls -l shadow.data/hosts/
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client1
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client2
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client3
drwxrwxr-x 2 user user 4096 Jun  2 16:54 server
```

Each host directory contains the output for each process running on that host.
For example:

```bash
$ ls -l shadow.data/hosts/client1/
-rw-rw-r-- 1 user user   1 Jun  2 16:54 curl.1000.exitcode
-rw-rw-r-- 1 user user   0 Jun  2 16:54 curl.1000.shimlog
-rw-r--r-- 1 user user   0 Jun  2 16:54 curl.1000.stderr
-rw-r--r-- 1 user user 542 Jun  2 16:54 curl.1000.stdout

$ cat shadow.data/hosts/client1/curl.1000.stdout
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
