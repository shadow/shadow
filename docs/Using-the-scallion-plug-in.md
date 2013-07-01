The scallion plug-in is used to experiment with the [Tor anonymity network](https://www.torproject.org/). The plug-in is a wrapper around [Tor's source code](https://gitweb.torproject.org/tor.git), and is most useful in conjunction with [[the browser plug-in|Using the browser plug-in]], [[the filetransfer plug-in|Using the filetransfer plug-in]], and [[the torrent plug-in|Using the torrent plug-in]] to transfer data across the anonymity network and measure performance characteristics.

The `resource/examples/scallion/` directory of the source distribution contains sample network configurations that work with Scallion, to get started with Tor experimentation.

## Scalability

The maximum memory requirements of our included sample network configurations are given below. Also included is the smallest possible EC2 instance required to run these configurations, as a convenience for selecting an EC2 instance type for [[Running Shadow on EC2]].

<table>
  <caption>Scallion Network Sizes and Memory Requirements</caption>
  <tr>
    <th>Size</th><th>RAM (GiB)</th><th>EC2 Instance</th>
  </tr>
  <tr>
    <td>tiny</td><td>&lt; 4</td><td>m1.large</td>
  </tr>
  <tr>
    <td>small</td><td>&lt; 16</td><td>m1.xlarge <i>or</i> m2.xlarge</td>
  </tr>
  <tr>
    <td>medium</td><td>&lt; 32</td><td>m2.2xlarge</td>
  </tr>
  <tr>
    <td>large</td><td>&lt; 64</td><td>m2.4xlarge</td>
  </tr>
</table>

<table>
  <caption>Scallion Network Node Breakdown by Type</caption>
  <tr>
    <th>Node Type | Size</th><th>Tiny</th><th>Small</th><th>Medium</th><th>Large</th>
  </tr>
  <tr>
    <td>Web Clients</td><td>190</td><td>475</td><td>950</td><td>2400</td>
  </tr>
  <tr>
    <td>Bulk Clients</td><td>10</td><td>25</td><td>50</td><td>180</td>
  </tr>
  <tr>
    <td>TorPerf 50KiB</td><td>5</td><td>10</td><td>20</td><td>75</td>
  </tr>
  <tr>
    <td>TorPerf 1MiB</td><td>5</td><td>10</td><td>20</td><td>75</td>
  </tr>
  <tr>
    <td>TorPerf 5MiB</td><td>5</td><td>10</td><td>20</td><td>75</td>
  </tr>
  <tr>
    <td>Web Servers</td><td>20</td><td>50</td><td>100</td><td>500</td>
  </tr>
  <tr>
    <td>Non-exit Relays</td><td>11</td><td>29</td><td>59</td><td>224</td>
  </tr>
  <tr>
    <td>Exit Relays</td><td>8</td><td>20</td><td>40</td><td>150</td>
  </tr>
  <tr>
    <td>Directory Authorities</td><td>1</td><td>1</td><td>1</td><td>1</td>
  </tr>
  <tr>
    <th>RAM required</th><th>&lt; 4</th><th>&lt; 16</th><th>&lt; 32</th><th>&lt; 64</th>
  </tr>
</table>

## Argument Usage

```xml
<application [...] arguments="arg1 arg2 arg3 [...]" />
```

The _arguments_ attribute of the _application_ XML element specifies application arguments for configuring a node's instance of the plug-in. Each argument is separated by a space.

Usage, by arg number:
   1. the scallion plug-in mode can be one of:
      + _dirauth_, for a Tor directory authority
      + _relay_, for a Tor non-exit relay
      + _exitrelay_, for a Tor exit relay
      + _client_, for a filetransfer HTTP client over a local Tor SOCKS proxy server
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
  <application plugin="filex" starttime="1" arguments="server 80 ~/.shadow/share/" />
</node>
<node id="webserver" bandwidthdown="102400" bandwidthup="102400" />
  <application plugin="filex" starttime="1" arguments="server 80 ../browser-example/" />
</node>
<node id="torrentauth" bandwidthdown="102400" bandwidthup="102400" >
  <application plugin="torrent" starttime="1" arguments="authority 5000"/>
</node>

<!-- our Tor network infrastructure -->

<node id="4uthority" >
  <application plugin="scallion" starttime="1" arguments="dirauth 1024 1024000 1024000 ./authority.torrc ./data/authoritydata ~/.shadow/share/geoip" />
</node>
<node id="exit" quantity="2" >
  <application plugin="scallion" starttime="60" arguments="exitrelay 1024 1024000 1024000 ./exit.torrc ./data/exitdata ~/.shadow/share/geoip" />
</node>
<node id="relay" quantity="2" >
  <application plugin="scallion" starttime="60" arguments="relay 1024 1024000 1024000 ./relay.torrc ./data/relaydata ~/.shadow/share/geoip" />
</node>

<!-- our Tor clients -->

<node id="fileclient" />
  <application plugin="scallion" starttime="600" arguments="client 1024 1024000 1024000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip" />
  <application plugin="filex" starttime="900" arguments="client single fileserver 80 localhost 9000 10 /1MiB.urnd" />
</node>
<node id="browserclient" />
  <application plugin="scallion" starttime="600" arguments="browser 1024 1024000 1024000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip" />
  <application plugin="filex" starttime="900" arguments="webserver 80 localhost 9000 6 /index.htm" />
</node>
<node id="torrentnode" quantity="3" />
  <application plugin="scallion" starttime="600" arguments="torrent 1024 1024000 1024000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip" />
  <application plugin="filex" starttime="900" arguments="torrent node torrentauth 5000 localhost 9000 6000 1MB" />
</node>
```

From the `resource/examples/scallion/` directory, save this file as `mytor.xml` and run it like:
```bash
scallion -i mytor.xml
```

## Generating your own Tor Network

If you don't want to use the Scallion examples included in the Shadow distribution or you want to customize the network, you can generate your own. Shadow contains a script to help you generate your own Tor network.

**NOTE**: this process assumes you have an Internet connection and that the Shadow base directory exists in your home directory at `~/`

### Prepare Alexa website data

The following produces `alexa-top-1000-ips.csv`: a csv list of the top 1000 servers.

```bash
wget http://s3.amazonaws.com/alexa-static/top-1m.csv.zip
gunzip top-1m.csv.zip
python ~/shadow/contrib/parsealexa.py
```

### Prepare Tor metrics data

This process requires lots of data and metrics from Tor. Be prepared to wait for the following downloads to complete.

**NOTE**: you may want to modify the URLs below to grab more up-to-date metrics data.

```bash
wget https://metrics.torproject.org/data/server-descriptors-2013-04.tar.bz2
tar xaf server-descriptors-2013-04.tar.bz2
wget https://metrics.torproject.org/data/extra-infos-2013-04.tar.bz2
tar xaf extra-infos-2013-04.tar.bz2
wget https://metrics.torproject.org/data/consensuses-2013-04.tar.bz2
tar xaf consensuses-2013-04.tar.bz2
wget https://metrics.torproject.org/csv/direct-users.csv
```

### Start generating!

**NOTES**:
  + You'll need to use one of the consensus files from the consensuses-2013-04 directory in the following steps.
  + You should have already build Shadow+Scallion (using './setup build') because this process requires the `tor` and `tor-gencert` binaries to complete successfully.

```python
export PATH=${PATH}:~/shadow/build/tor/src/or:~/shadow/build/tor/src/tools
mkdir mytor
cd mytor
python ~/shadow/contrib/generate.py --help
python ~/shadow/contrib/generate.py --nauths 1 --nrelays 20 --nclients 200 --nservers 20 --fim 0.0 --fweb 0.90 --fp2p 0.0 --fbulk 0.10 --nperf50k 10 --nperf1m 10 --nperf5m 10 ../alexa-top-1000-ips.csv ../2013-04-30-23-00-00-consensus ../server-descriptors-2013-04/ ../extra-infos-2013-04/ ../direct-users-2013-04.csv
```

If everything went smoothly, `scallion` can be run from inside the 'mytor' directory as usual.
```