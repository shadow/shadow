This page discusses how to prepare your machine to begin running Shadow experiments.
## installing dependencies

Shadow relies on the following tools and libraries to function properly. Versions and plug-in-specific dependencies are noted in parenthesis where applicable

**Required**:
* gcc
* make
* xz-utils
* python (= 2.7)
* cmake (>= 2.8)
* glib (>= 2.28.8)
* automake (scallion plug-in only)
* autoconf (scallion plug-in only)
* libtidy (scallion and browser plug-ins only)

**Recommended**:
* dstat
* pdftk
* matplotlib
* numpy
* scipy

To install these using the Fedora package manager, try something like:
```bash
sudo yum install -y gcc xz make automake autoconf cmake libtidy libtidy-devel glib2 glib2-devel dstat pdftk python python-matplotlib numpy scipy
```
On Ubuntu, try:
```bash
sudo apt-get -y install gcc xz-utils make automake autoconf cmake tidy libtidy-dev libglib2.0 libglib2.0-dev dstat pdftk python2.7 python-matplotlib python-numpy python-scipy 
```
These may also be downloaded and installed locally if preferred.

## obtaining Shadow

For best results, release versions are recommended and can be obtained in various ways:
* by visiting https://shadow.cs.umn.edu/download/
* by visiting https://github.com/shadow/shadow/downloads
* by using git (see below)

Using git to obtain the latest development changes:
```bash
git clone https://github.com/shadow/shadow.git
cd shadow
```
You can also use git to obtain the latest stable release by also running:  
```bash
git checkout release
```
## manual dependencies

_This section applies only if you want to run Tor using the scallion plug-in_

You've downloaded Shadow and changed to its **top-level directory**. Next, you'll want to take care of some manual dependencies required to build Tor as a Shadow plug-in. We'll need to install **openssl** and **libevent** after downloading and building them with custom configuration options.

Luckily, Shadow contains a script to do this for you, should you so desire:
```bash
./contrib/installdeps.sh
```
This script will ask before downloading anything and install openssl and libevent to `~/.shadow` by default. To change the install location, set the envrionment variable `PREFIX` before running the script, like:
```bash
PREFIX=/somewhere/else ./contrib/installdeps.sh
```
If you set `PREFIX` as above, note the install location `/somewhere/else` as you will need it when installing Shadow.

If you prefer to do things manually, please see the `contrib/installdeps.sh` script for the steps required. You'll need to configure openssl with something like `./config --prefix=/home/rob/.shadow shared threads -fPIC` and libevent with something like `./configure --prefix=/home/rob/.shadow --enable-shared CFLAGS="-fPIC -I/home/rob/.shadow" LDFLAGS="-L/home/rob/.shadow"`.

## building and installing Shadow and its plug-ins

Shadow's setup.py script will help you configure, build, and install Shadow. It has 
extensive help menus which can be accessed with:
```bash
python setup.py --help
python setup.py build --help
python setup.py install --help
```
Shadow does not require root privileges, and the default and recommended setup
is to install to `~/.shadow`:
```bash
python setup build
python setup install
```

Important notes:  
+ The two most useful build options are `-g` or `--debug` to build Shadow with debugging symbols, and `--tor-prefix` to build Scallion with your local custom Tor distribution (instead of downloading one from torproject.org). 

+ If you installed any dependencies somewhere other than `~/.shadow`, you should use the `--include` and `--library` flags, and if you want to install Shadow somewhere besides `~/.shadow`, you should use the `--prefix` flag.

+ It will probably be useful to add `~/.shadow/bin` (or `/bin` in your non-default install prefix) to your `PATH` following installation.

Other notes:  
+ All build output is generated out-of-source, by default to the `./build` directory.
+ The setup.py script is a wrapper to `cmake` and `make`. Using `cmake` and `make` directly is also possible, but strongly discouraged. 
+ When you install Shadow you are installing the Shadow binary (`shadow-bin`) and an additional python wrapper script (`shadow`) that assists in running the Shadow binary, as well as various built-in plug-ins.

## system configs and limits

There is a default linux system limit on the number of open files. If each node 
in your Shadow plug-in opens many file or socket descriptors (if you have many nodes, this is very likely to happen), you'll likely want to increase the limit so you application doesn't start getting errors when calling `open()` or `socket()`.

You can check the maximum number of open file descriptors allowed in your _current session_:
```bash
ulimit -n
```
And you can check the _system wide_ limits with:
```bash
cat /proc/sys/fs/file-nr
```
That tells you:
 1. the system-wide number of open file handles
 1. the system-wide number of free handles
 1. and the system-wide limit on the maximum number of open files for all processes

You will want to raise the limits by modifying `/etc/security/limits.conf` and rebooting.
For example, to handle all our network configurations on EC2, I use:
```
* soft nofile 25000
* hard nofile 25000
```
You can watch `/proc/sys/fs/file-nr` and reduce the limit to something less than 25000 according to your usage, if you'd like.

For more information:
```bash
man proc
man ulimit -n
cat /proc/sys/fs/file-max
cat /proc/sys/fs/inode-max
```