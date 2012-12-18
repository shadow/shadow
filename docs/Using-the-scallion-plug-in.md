The scallion plug-in is used to experiment with the [Tor anonymity network](https://www.torproject.org/). The plug-in is a wrapper around [Tor's source code](https://gitweb.torproject.org/tor.git), and utilizes code from [[the browser plug-in|Using the browser plug-in]], [[the filetransfer plug-in|Using the filetransfer plug-in]], and [[the torrent plug-in|Using the torrent plug-in]] to transfer data across the anonymity network and measure performance characteristics.

The `resource/scallion-hosts` directory of the source distribution contains sample network configurations that work with Scallion, to get started with Tor experimentation.

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

## Argument Usage

```xml
<software [...] arguments="arg1 arg2 arg3 [...]" />
```

The _arguments_ attribute of the _software_ XML element specifies application arguments for configuring a node's instance of the plug-in. Each argument is separated by a space.

Usage:
   1. the plug-in mode can be one of:
      + _dirauth_, for a Tor directory authority
      + _relay_, for a Tor non-exit relay
      + _exitrelay_, for a Tor exit relay
      + _client_, for a filetransfer HTTP client over a local Tor SOCKS proxy server
      + _torrent_, for a torrent P2P client over a local Tor SOCKS proxy server
      + _browser_, for a browser client over a local Tor SOCKS proxy server
   1. _weight_, the bandwidth _weight_ that should appear in the Tor consensus for this relay, in KiB
   1. _rate_, the global _rate limit_ for this Tor in bytes, passed as Tor's `--BandwidthRate` option
   1. _burst_, the global _burst limit_ for this Tor in bytes, passed as Tor's `--BandwidthBurst` option
   1. _torrc_, the path to the _torrc file_ for this Tor, passed as Tor's `-f` option
   1. _datadir_, the path to the _base data directory_ for this Tor, passed as Tor's `--DataDirectory` option
   1. _geoip_, the path to the _geoip file_ for this Tor, passed as Tor's `--GeoIPFile` option

## Example

Here is an example XML file that contains each type of Tor node possible to configure:

```xml
<!-- our network -->

<cluster id="vnet" bandwidthdown="1024" bandwidthup="768" />
<link clusters="vnet vnet" latency="60" jitter="20" packetloss="0.0" />

<!-- the plug-ins we will be using -->

<plugin id="filex" path="~/.shadow/plugins/libshadow-plugin-filetransfer.so" />
<plugin id="torrent" path="~/.shadow/plugins/libshadow-plugin-torrent.so" />
<plugin id="scallion" path="~/.shadow/plugins/libshadow-plugin-scallion.so" />

<!-- the length of our experiment in seconds -->

<kill time="1800" />

<!-- our services -->


<node id="fileserver" bandwidthdown="102400" bandwidthup="102400" >
  <application plugin="filex" time="1" arguments="server 80 ~/.shadow/share/" />
</node>
<node id="webserver" bandwidthdown="102400" bandwidthup="102400" />
  <application plugin="filex" time="1" arguments="server 80 ../browser-example/" />
</node>
<node id="torrentauth" bandwidthdown="102400" bandwidthup="102400" >
  <application plugin="torrent" time="1" arguments="authority 5000"/>
</node>

<!-- our Tor network infrastructure -->


<node id="4uthority" software="authorityapp" >
  <application plugin="scallion" time="1" arguments="dirauth 1024 1024000 1024000 ./authority.torrc ./data/authoritydata ~/.shadow/share/geoip" />
</node>
<node id="exit" software="exitapp" quantity="2" >
  <application plugin="scallion" time="60" arguments="exitrelay 1024 1024000 1024000 ./exit.torrc ./data/exitdata ~/.shadow/share/geoip" />
</node>
<node id="relay" software="relayapp" quantity="2" >
  <application plugin="scallion" time="60" arguments="relay 1024 1024000 1024000 ./relay.torrc ./data/relaydata ~/.shadow/share/geoip" />
</node>

<!-- our Tor clients -->


<node id="fileclient" />
  <application plugin="scallion" time="600" arguments="client 1024 1024000 1024000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip client single fileserver 80 localhost 9000 10 /1MiB.urnd" />
</node>
<node id="browserclient" />
  <application plugin="scallion" time="600" arguments="browser 1024 1024000 1024000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip webserver 80 localhost 9000 6 /index.htm" />
</node>
<node id="torrentnode" quantity="3" />
  <application plugin="scallion" time="600" arguments="torrent 1024 1024000 1024000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip torrent node torrentauth 5000 localhost 9000 6000 1MB" />
</node>
```

From the `resource/scallion-example` directory, save this file as `mytor.xml` and run it like:
```bash
scallion -y -i mytor.xml
```