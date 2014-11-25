The scallion plug-in is used to experiment with the [Tor anonymity network](https://www.torproject.org/). The plug-in is a wrapper around [Tor's source code](https://gitweb.torproject.org/tor.git), and is most useful in conjunction with [[the filetransfer plug-in|Using the filetransfer plug-in]] to transfer data across the anonymity network and measure performance characteristics.

## Dependencies

YUM (Fedora):

```bash
sudo yum install -y gcc automake autoconf zlib zlib-devel
```

APT (Ubuntu):

```bash
sudo apt-get -y install gcc automake autoconf zlib1g-dev
```

## Setup and Install

```bash
git clone https://github.com/shadow/shadow-plugin-tor.git -b release
```

Next, we'll need to setup **openssl** and **libevent** with custom configuration options in order to run Tor in Shadow. The setup script should handle this for you (run `./setup --help` for more details).

```bash
./setup dependencies
```

If you instead prefer to do this manually, download openssl and libevent and run:

```bash
cd openssl
./config --prefix=/home/${USER}/.shadow shared threads enable-ec_nistp_64_gcc_128 -fPIC
make
make install_sw
cd ../libevent
./configure --prefix=/home/${USER}/.shadow --enable-shared CFLAGS="-fPIC -I/home/${USER}/.shadow" LDFLAGS="-L/home/${USER}/.shadow"
make
make install
```

Finally, finish the installation:

```bash
./setup build
./setup install
export PATH=${PATH}:/home/${USER}/.shadow/bin
```

## Running Tor in Shadow

Example experiment configurations can be found in the `shadow-plugin-tor/resource` directory. Please see the `minimal.tar.xz` example to understand how to configure your nodes to run Tor.

Your `shadow.config.xml` file must specify that the Tor plug-in should be loaded, and that a should run an instance of the Tor plugin as an application, e.g.:

```xml
<plugin id="tor" path="~/.shadow/plugins/libshadow-plugin-tor.so" />
<node id="relay">
  <application plugin="tor" arguments="[...]"  [...] />
</>
```

The _arguments_ attribute of the _application_ XML element specifies application arguments for configuring a node's instance of the plug-in. The arguments will be passed directly to Tor, and so all options as specified in [the Tor config manual](https://www.torproject.org/docs/tor-manual-dev.html.en) are allowed.

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
    <th>Total Clients</th><th>180</th><th>375</th><th>750</th><th>1800</th>
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
    <th>Total Relays</th><th>49</th><th>99</th><th>198</th><th>499</th>
  </tr>
  <tr>
    <th>Web Servers</th><th>20</th><th>50</th><th>100</th><th>500</th>
  </tr>
  <tr>
    <th>RAM Required (GiB)</th><td>&lt; 4</td><td>&lt; 12</td><td>&lt; 24</td><td>&lt; 60</td>
  </tr>
  <tr>
    <th>EC2 Instance</th><td>m1.large</td><td>m1.xlarge <i>or</i><br /> m2.xlarge</td><td>m2.2xlarge</td><td>m2.4xlarge</td>
  </tr>
</table>

\* For the large configuration, you might run up against open file limits (messages from scallion: `Couldn't open ... for locking: Too many open files` and `Acting on config options left us in a broken state. Dying.`). Increasing the limit (e.g., with `ulimit -n`) to 16384 resolves the issue, but lower limits might work, too.

## Generating a new Tor Network

If you don't want to use the pre-generated examples in `shadow-plugin-tor/resource` or you want to customize the network, you can generate your own. **The following process assumes you have an Internet connection and that the Shadow base directory exists in your home directory at `~/`.**

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

### scallion info

`scallion` is a plug-in that runs the [Tor anonymity software](https://www.torproject.org/), allowing us to configure a private Tor networks on our machine and transfer data through it. Scallion requires an additional library in `LD_PRELOAD` and also requires setting some extra environment variables. Because of these complexities, a python helper script called `scallion` is also installed in `~/.shadow/bin` (or `your/prefix/bin`). This script is a wrapper for the `shadow` wrapper script.

Tor requires other configuration files and keys to function. A very small example can be run as follows:

```bash
cd resource/examples/scallion/
tar xaf minimal.tar.xz
cd minimal
scallion -i shadow.config.xml
```

Scallion notes:
+ Each Tor node type may be configured in the `*torrc` files
+ The `scallion` script automatically redirects all output from Tor into `./data`
+ The `./data` directory contains the private data directories from each Tor instance running in the experiment
+ All Shadow and Tor logging for every node is redirected to the `./data/scallion.log` file
+ If `dstat` is installed, its output is redirected to `./data/dstat.log`

The above toy example is not realistic for research purposes. More realistic network configurations can be found in the other compressed files in `resource/examples/scallion/`. To run one of these experiments with 2 worker threads enabled:

```bash
cd resource/examples/scallion/
tar xaf tiny-m1.large.tar.xz
cd tiny-m1.large
scallion -i shadow.config.xml -w 2
```

Note that these experiments will take on the order of 30 minutes to several hours, and consume ~4 to ~64 GiB of RAM, depending which size you run (tiny, small, medium, large). See [[Using the scallion plug-in]] for more information and for details on generating your own Scallion `shadow.config.xml` file, and [[Analyzing results]] for help parsing the output.

### analyzing scallion

Suppose we want to test the performance difference between 2 of Tor's schedulers. We could do the following to setup our experiments:

```bash
cd resource
tar xaf tiny-m1.large.tar.xz
mv tiny-m1.large vanilla
tar xaf tiny-m1.large.tar.xz
mv tiny-m1.large priority
```

At this point you should add the string `CircuitPriorityHalflife 30` to the end of each of the torrc files located in the `priority` directory. This will enable the scheduler that prioritizes circuits based on exponentially-weighted moving average circuit throughputs.

Now you can run both experiments and plot the results:  (**NOTE**: _each experiment may take up to an hour to run, so be patient_)

```bash
cd vanilla
scallion -y
cd ../priority
scallion -y
cd ../
```

Since the `scallion` script redirects log messages to `data/scallion.log`, the following commands can be used to parse and plot those results:

```bash
python ../../contrib/analyze.py parse --output vanilla-results vanilla/data/scallion.log
python ../../contrib/analyze.py parse --output priority-results priority/data/scallion.log
python ../../contrib/analyze.py plot --title "Shadow Scheduler Test" --prefix "scheduler" --data vanilla-results/ "vanilla" --data priority-results/ "priority"
```

See any of the graphs in `./graphs`, or if you have `pdftk` installed, you can simply view the `scheduler-combined.pdf` file.