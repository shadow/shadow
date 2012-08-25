**PLEASE NOTE**: _this page is currently incomplete and in-progress_
## installing dependencies
Shadow relies on the following tools and libraries to function properly. Versions and plug-in-specific dependencies are noted in parenthesis where applicable:
* gcc
* make
* xz-utils
* python (= 2.7)
* cmake (>= 2.8)
* glib (>= 2.28.8)
* libtidy (browser plug-in only)

To install these using the Fedora package manager, try something like:
```bash
sudo yum install -y gcc xz python cmake libtidy libtidy-devel glib2 glib2-devel
```
On Ubuntu, try:
```bash
sudo apt-get -y install gcc xz-utils python2.7 cmake tidy libtidy-dev libglib2.0 libglib2.0-dev
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
If you prefer to do things manually, please see the `contrib/installdeps.sh` script for the steps required. You'll need to configure openssl with something like `./config --prefix=/home/rob/.shadow shared threads -fPIC` and libevent with something like `./configure --prefix=/home/rob/.shadow --enable-shared CFLAGS="-fPIC -I/home/rob/.shadow" LDFLAGS="-L/home/rob/.shadow"`.

## building and installing Shadow and its plug-ins

## system configs and limits