This page discusses how to prepare your machine to begin running Shadow experiments.
## installing dependencies

Shadow relies on the following tools and libraries to function properly. Versions and plug-in-specific dependencies are noted in parenthesis where applicable

**Required**:
* clang, llvm (3.2)
* glib (>= 2.32.0)
* cmake (>= 2.8.8)
* make
* python (= 2.7)
* xz-utils
* gcc (scallion plug-in only)
* automake (scallion plug-in only)
* autoconf (scallion plug-in only)
* libtidy (browser plug-in only)

**Recommended**:
* htop
* screen
* dstat
* numpy
* scipy
* matplotlib
* pdftk

To install these using the Fedora package manager, try something like:
```bash
sudo yum install -y gcc xz make automake autoconf cmake libtidy libtidy-devel glib2 glib2-devel python htop screen dstat numpy scipy python-matplotlib pdftk
```
On Ubuntu, try:
```bash
sudo apt-get -y install gcc xz-utils make automake autoconf cmake tidy libtidy-dev libglib2.0 libglib2.0-dev dstat pdftk python2.7 python-matplotlib python-numpy python-scipy htop screen
```
These may also be downloaded and installed locally if preferred.  

You'll also need to manually build and install **clang/llvm** from source because for some reason the OS packages do not include the shared CMake module files Shadow requires.  

First install clang/llvm dependencies:

```bash
sudo yum install -y libxml2-devel libxslt-devel
```

Then try the following (replace 'username' with your username and 'N' with the number of threads for a parallel build):

```bash
wget http://www.llvm.org/releases/3.2/llvm-3.2.src.tar.gz
wget http://www.llvm.org/releases/3.2/clang-3.2.src.tar.gz
tar xaf llvm-3.2.src.tar.gz
tar xaf clang-3.2.src.tar.gz
cp -R clang-3.2.src llvm-3.2.src/tools/clang
cd llvm-3.2.src
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/home/username/.local ../.
make -jN
make install
```

## obtaining Shadow

For best results, release versions are recommended and can be obtained in various ways:
* by visiting https://shadow.cs.umn.edu/download/
* by visiting https://github.com/shadow/shadow/tags
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

## building and installing Shadow and its plug-ins

You've downloaded Shadow and changed to its **top-level directory**. Next, you'll want to take care of some manual dependencies required to build Tor as a Shadow plug-in during a full build. We'll need to install **openssl** and **libevent** after downloading and building them with custom configuration options.

Luckily, Shadow contains a script to do this for you, and will help you configure, build, and install Shadow. It has 
extensive help menus which can be accessed with:
```bash
python setup.py --help
python setup.py dependencies --help
python setup.py build --help
python setup.py install --help
```
Shadow does not require root privileges, and the default and recommended setup
is to install to `~/.shadow`:
```bash
python setup.py dependencies
python setup.py build
python setup.py install
```

If you prefer to install **openssl** and **libevent** manually, you'll need to configure openssl with something like `./config --prefix=/home/rob/.shadow shared threads -fPIC` and libevent with something like `./configure --prefix=/home/rob/.shadow --enable-shared CFLAGS="-fPIC -I/home/rob/.shadow" LDFLAGS="-L/home/rob/.shadow"`.

Important notes:  
+ The two most useful build options are `-g` or `--debug` to build Shadow with debugging symbols, and `--tor-prefix` to build Scallion with your local custom Tor distribution (instead of downloading one from torproject.org). 

+ If you installed any dependencies somewhere other than `~/.shadow`, you should use the `--include`, `--library`, `--openssl-prefix` and `--libevent-prefix` flags, and if you want to install Shadow somewhere besides `~/.shadow`, you should use the `--prefix` flag.

+ It will probably be useful to add `~/.shadow/bin` (or `/bin` in your non-default install prefix) to your `PATH` following installation.

Other notes:  
+ All build output is generated out-of-source, by default to the `./build` directory.
+ The setup.py script is a wrapper to `cmake` and `make`. Using `cmake` and `make` directly is also possible, but strongly discouraged. 
+ When you install Shadow you are installing the Shadow binary (`shadow-bin`) and an additional python wrapper script (`shadow`) that assists in running the Shadow binary, as well as various built-in plug-ins. You can avoid building plug-ins using the '--disable-plugin-*' setup script options.

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
