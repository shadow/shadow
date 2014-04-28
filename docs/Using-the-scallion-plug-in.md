The scallion plug-in is used to experiment with the [Tor anonymity network](https://www.torproject.org/). The plug-in is a wrapper around [Tor's source code](https://gitweb.torproject.org/tor.git), and is most useful in conjunction with [[the filetransfer plug-in|Using the filetransfer plug-in]] to transfer data across the anonymity network and measure performance characteristics.

The `resource/examples/scallion/` directory of the source distribution contains sample network configurations that work with Scallion, to get started with Tor experimentation.

## Scalability

The maximum memory requirements of our included sample network configurations are given below. Also included is the smallest possible EC2 instance required to run these configurations, as a convenience for selecting an EC2 instance type for [[Running Shadow on EC2]].

<table>
  <caption>Scallion Network Node Breakdown by Type, and Memory Requirements</caption>
  <tr>
    <th><u>Node Type</u></th><th><u>Tiny</u></th><th><u>Small</u></th><th><u>Medium</u></th><th><u>Large</u> *</th>
  </tr>
  <tr>
    <td>Web Clients</td><td>135</td><td>270</td><td>540</td><td>1350</td>
  </tr>
  <tr>
    <td>Bulk Clients</td><td>15</td><td>30</td><td>60</td><td>150</td>
  </tr>
  <tr>
    <td>TorPerf 50KiB</td><td>10</td><td>25</td><td>50</td><td>100</td>
  </tr>
  <tr>
    <td>TorPerf 1MiB</td><td>10</td><td>25</td><td>50</td><td>100</td>
  </tr>
  <tr>
    <td>TorPerf 5MiB</td><td>10</td><td>25</td><td>50</td><td>100</td>
  </tr>
  <tr>
    <td><b>Total Clients</b></td><td><b>180</b></td><td><b>375</b></td><td><b>750</b></td><td><b>1800</b></td>
  </tr>
  <tr>
    <td>Guard Relays</td><td>11</td><td>22</td><td>45</td><td>116</td>
  </tr>
  <tr>
    <td>Exit Relays</td><td>7</td><td>15</td><td>30</td><td>76</td>
  </tr>
  <tr>
    <td>Guard+Exit Relays</td><td>4</td><td>8</td><td>16</td><td>41</td>
  </tr>
  <tr>
    <td>Middle Relays</td><td>26</td><td>52</td><td>104</td><td>262</td>
  </tr>
  <tr>
    <td>Directory Authorities</td><td>1</td><td>2</td><td>3</td><td>4</td>
  </tr>
  <tr>
    <td><b>Total Relays</b></td><td><b>49</b></td><td><b>99</b></td><td><b>198</b></td><td><b>499</b></td>
  </tr>
  <tr>
    <td>Web Servers</td><td>20</td><td>50</td><td>100</td><td>500</td>
  </tr>
  <tr>
    <th>RAM Required (GiB)</th><td>&lt; 4</td><td>&lt; 12</td><td>&lt; 24</td><td>&lt; 64</td>
  </tr>
  <tr>
    <th>EC2 Instance</th><td>m1.large</td><td>m1.xlarge <i>or</i><br /> m2.xlarge</td><td>m2.2xlarge</td><td>m2.4xlarge</td>
  </tr>
</table>

\* For the large configuration, you might run up against open file limits (messages from scallion: `Couldn't open ... for locking: Too many open files` and `Acting on config options left us in a broken state. Dying.`). Increasing the limit (e.g., with `ulimit -n`) to 16384 resolves the issue, but lower limits might work, too.

## Argument Usage

```xml
<application [...] arguments="arg1 arg2 [...]" />
```

The _arguments_ attribute of the _application_ XML element specifies application arguments for configuring a node's instance of the plug-in. Each argument is separated by a space.

Usage, by arg number:
   1. the scallion plug-in mode can be one of:
      + _dirauth_, for a Tor directory authority
      + _hsauth_, for a Tor hidden service authority
      + _bridgeauth_, for a Tor bridge authority
      + _relay_, for a Tor relay that rejects exit traffic
      + _exitrelay_, for a Tor relay that allows exit traffic
      + _client_, for a Tor client connecting over a local Tor SOCKS proxy server
      + _bridge_, for a Tor client that also acts as a bridge
      + _bridgeclient_, for a Tor client that connects through a Tor bridge
   1. _weight_, the bandwidth _weight_ that should appear in the Tor consensus for this relay, in KiB
   1. _[...]_, other options as specified in [the Tor config manual](https://www.torproject.org/docs/tor-manual-dev.html.en)

## Example

An example XML file that contains each type of Tor node possible to configure can be found in the `resource/examples/scallion/minimal` directory.

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