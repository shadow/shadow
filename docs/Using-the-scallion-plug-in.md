The scallion plug-in is used to experiment with the [Tor anonymity network](https://www.torproject.org/). The plug-in is a wrapper around [Tor's source code](https://gitweb.torproject.org/tor.git), and utilizes code from [[the browser plug-in|Using the browser plug-in]], [[the filetransfer plug-in|Using the filetransfer plug-in]], and [[the torrent plug-in|Using the torrent plug-in]] to transfer data across the anonymity network and measure performance characteristics.

The `resource/scallion-hosts` directory of the source distribution contains sample network configurations that work with Scallion, to get started with Tor experimentation.

## Argument Usage

```xml
<software [...] arguments="arg1 arg2 arg3 [...]" />
```

The _arguments_ attribute of the _software_ XML element specifies application arguments for configuring a node's instance of the plug-in. Each argument is separated by a space.

Usage:
   1. the plug-in mode can be one of:
      + _dirauth_, for a Tor directory authority
      + _relay_, for a Tor non-exit relay
      + _exitrelay, for a Tor exit relay
      + _client_, for a filetransfer HTTP client over a local Tor SOCKS proxy server
      + _torrent_, for a torrent P2P client over a local Tor SOCKS proxy server
      + _browser_, for a browser client over a local Tor SOCKS proxy server
   1. the bandwidth _weight_ that should appear in the Tor consensus for this relay, in KiB
   1. the global _rate limit_ for this Tor in bytes, passed as Tor's `--BandwidthRate` option
   1. the global _burst limit_ for this Tor in bytes, passed as Tor's `--BandwidthBurst` option
   1. the path to the _torrc file_ for this Tor, passed as Tor's `-f` option
   1. the path to the _base data directory_ for this Tor, passed as Tor's `--DataDirectory` option
   1. the path to the _geoip file_ for this Tor, passed as Tor's `--GeoIPFile` option
   1. if the first argument was _client_, _torrent_, or _browser_, then the required arguments for each of those plugins should be appended to the above options

## Example

Here is an example XML file that contains each type of Tor node possible to configure:

```xml
[...]
```

From the `resource/scallion-example` directory, save this file as `mytor.xml` and run it like:
```bash
scallion -y -i mytor.xml
```

## Scalability

The maximum memory requirements of our included sample network configurations are given below. Also included is the smallest possible EC2 instance required to run these configurations, as a convenience for selecting an EC2 instance type for [[Running Shadow on EC2]].

<table>
  <caption>Scallion Network Sizes and Memory Requirements</caption>
  <tr>
    <th>Size</th><th>Number of Nodes</th><th>RAM (GiB)</th><th>EC2 Instance</th>
  </tr>
  <tr>
    <td>tiny</td><td>20 relays, 200 clients</td><td>&lt; 4</td><td>m1.large</td>
  </tr>
  <tr>
    <td>small</td><td>50 relays, 500 clients</td><td>&lt; 16</td><td>m1.xlarge <i>or</i> m2.xlarge</td>
  </tr>
  <tr>
    <td>medium</td><td>100 relays, 1000 clients</td><td>&lt; 32</td><td>m2.2xlarge</td>
  </tr>
  <tr>
    <td>large</td><td>250 relays, 2500 clients</td><td>&lt; 64</td><td>m2.4xlarge</td>
  </tr>
</table>