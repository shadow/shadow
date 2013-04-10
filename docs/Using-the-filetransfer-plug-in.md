The filetransfer plug-in is used to transfer files between virtual nodes. It contains very simple and custom implementations of an HTTP client and an HTTP server. The client sends GET requests to the server, and the server responds with the file contents.

## Argument Usage

```xml
<application [...] arguments="arg1 arg2 arg3 [...]" />
```

The _arguments_ attribute of the _application_ XML element specifies application arguments for configuring a node's instance of the plug-in application. Each argument is separated by a space. For the filetransfer plug-in, the first item of the _arguments_ attribute specifies the mode, one of either _client_ or _server_, and _client_ mode can either be _single_ mode (download _n_ times from one server) or _multi_ mode (download multiple times from multiple servers).

### Usage for _server_ mode:
   1. the string 'server'
   1. the port on which the server will listen for connections
   1. the path to the document root to be served

### Usage for _client single_ mode:
   1. the string 'client'
   1. the string 'single'
   1. the file server's hostname
   1. the port on which to connect to the HTTP server (the server's listen port)
   1. the string 'none', or the hostname of a SOCKS proxy server
   1. the string '0', or the port on which to connect to the SOCKS proxy server
   1. the number of times to download the file
   1. the path of the file to download, relative to the server's docroot (must begin with '/')

### Usage for _client multi_ mode:
   1. the string 'client'
   1. the string 'multi'
   1. the path to a _download specification file_
   1. the string 'none', or the hostname of a SOCKS proxy server
   1. the string '0', or the port on which to connect to the SOCKS proxy server
   1. the path to a _think-time CDF file_
   1. the string '-1', or the maximum number of seconds to run before shutting down
   1. the string '-1', or the maximum number of files to download before shutting down

Each line of a _download specification file_ should contain three items separated by a ':' (colon): a server's name, the server's port, and the file to download. This allows specification of multiple files from multiple servers. For each download, the client chooses a random line and downloads as specified. The format is like:
```text
server1name:80:/myfile1
server2name:80:/myfile2
```

Each line of a _think-time CDF file_ should contain two items separated by a ' ' (space): the cumulative value of the think time in milliseconds (the time to pause after finishing one download and before starting the next), and the percentile of that value (between 0 and 1).
```text
1000.000 0.2000000000
2000.000 0.4000000000
3000.000 0.6000000000
4000.000 0.8000000000
5000.000 1.0000000000
```

## Simple Example

To simulate file downloads of **X** KiB each, first create a file in what will become our server root directory:

```bash
mkdir docroot
dd if=/dev/urandom of=docroot/myfile bs=1024 count=X
```

Then, we create a hosts.xml with the following:
```xml
<plugin id="filex" path="~/.shadow/plugins/libshadow-plugin-filetransfer.so" />

<node id="servername">
  <application plugin="filex" starttime="10" arguments="server 80 docroot/" />
</node>

<node id="clientname">
  <application plugin="filex" starttime="20" arguments="client single servername 80 none 0 5 /myfile" />
</node>

<kill time="600" />
```

Make sure you have the `~/.shadow/share/topology.xml` file installed, then run the example:
```bash
shadow ~/.shadow/share/topology.xml hosts.xml | grep fg-download-complete
```

The output should look like:
```text
[fg-download-complete] got first bytes in 0.229 seconds and 10240 of 10240 bytes in 0.294 seconds (download 1 of 5)
[fg-download-complete] got first bytes in 0.208 seconds and 10240 of 10240 bytes in 0.272 seconds (download 2 of 5)
[fg-download-complete] got first bytes in 0.443 seconds and 10240 of 10240 bytes in 0.813 seconds (download 3 of 5)
[fg-download-complete] got first bytes in 0.213 seconds and 10240 of 10240 bytes in 0.420 seconds (download 4 of 5)
[fg-download-complete] got first bytes in 0.321 seconds and 10240 of 10240 bytes in 0.478 seconds (download 5 of 5)
```

## Implementation

The server can handle multiple connections at once to various clients, but only one file may be downloaded over each connection. The client can only download one file at a time.