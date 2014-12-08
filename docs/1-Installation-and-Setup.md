## Installing Dependencies

Shadow relies on the following tools and libraries to function properly. Versions and plug-in-specific dependencies are noted in parenthesis where applicable

**Required**:
* clang, llvm (>= 3.2)
* glib (>= 2.32.0)
* igraph (>= 0.5.4)
* cmake (>= 2.8.8)
* make
* python (= 2.7)
* xz-utils
* gcc (scallion plug-in only)
* automake (scallion plug-in only)
* autoconf (scallion plug-in only)
* zlib (scallion plug-in only)

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
sudo yum install -y xz make cmake glib2 glib2-devel igraph igraph-devel python htop screen dstat numpy scipy python-matplotlib pdftk libxml2-devel libxslt-devel git wget gcc-c++
```

On Ubuntu, try:

```bash
sudo apt-get -y install xz-utils make cmake libglib2.0 libglib2.0-dev libigraph0 libigraph0-dev dstat pdftk python2.7 python-matplotlib python-numpy python-scipy htop screen libxml2-dev libxslt-dev git
```

## Shadow Setup

```bash
git clone https://github.com/shadow/shadow.git -b release
cd shadow
./setup build
./setup install
export PATH=${PATH}:/home/${USER}/.shadow/bin
```

### Important Notes

+ You should add `/home/${USER}/.shadow/bin` to your shell setup for the PATH environment variable (e.g., in `~/.bashrc` or `~/.bash_profile`)

+ The two most useful build options are:  
 + `-g` or `--debug` to build Shadow with debugging symbols
 + `--tor-prefix` to build Scallion with your local custom Tor distribution (instead of downloading one from torproject.org).
 + if you want to use valgrind and don't want it to report spurious openssl errors, add this to the end of the openssl configure line: `-g -pg -DPURIFY -Bsymbolic`

+ If you installed any dependencies somewhere other than `~/.shadow`, you should use the following flags during the build process:
 + `--include`
 + `--library`
 + `--openssl-prefix`
 + `--libevent-prefix`

+ If you want to install Shadow somewhere besides `~/.shadow`, you should use the `--prefix` flag.

### Other Notes

+ All build output is generated out-of-source, by default to the `./build` directory.
+ The `setup` script is a wrapper to `cmake` and `make`. Using `cmake` and `make` directly is also possible, but strongly discouraged. 
+ When you install Shadow you are installing the Shadow binary (`shadow-bin`) and an additional python wrapper script (`shadow`) that assists in running the Shadow binary, as well as various built-in plug-ins. You can avoid building plug-ins using the '--disable-plugin-*' setup script options.

## System Configs and Limits

### Number of Open Files

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

### Number of Maps

There is a system limit on the number of `mmap()` mappings per process. The limit can be queried in these ways:

```bash
sysctl vm.max_map_count
cat /proc/sys/vm/max_map_count
```

You can check the number of maps currently used in a process with pid=PID like this:

```bash
cat /proc/PID/maps | wc -l
```

Most users will not have to modify these settings. However, if an application running in Shadow makes extensive use of `mmap()`, you may need to increase the limit. You can do that at run time with:

```bash
sudo sysctl -w vm.max_map_count=262144
```

If you also want to change it at run time AND make it persistent, you can use:

```bash
sudo echo "vm.max_map_count = 262144" >> /etc/sysctl.conf
sudo sysctl -p
```

For more information:
https://www.kernel.org/doc/Documentation/sysctl/vm.txt

# Shadow in the Cloud

Amazon’s [Elastic Compute Cloud (EC2)](http://aws.amazon.com/ec2/) infrastructure provides a simple and relatively [cost-efficient](http://aws.amazon.com/ec2/#pricing) way to run large-scale Shadow experiments without the need to buy expensive hardware or manage complex configurations. You can get started running Shadow experiments on EC2 in minutes using our pre-configured public EC2 AMI, which has already been set up to run Shadow.

1. Sign up for [Amazon EC2 access](https://aws-portal.amazon.com/gp/aws/developer/registration)
1. Launch an instance using our pre-installed and configured [Shadow-cloud AMI (ami-0f70c366)](https://console.aws.amazon.com/ec2/home?region=us-east-1#launchAmi=ami-0f70c366) based on Ubuntu-12.04 LTS
1. Follow the New Instance Wizard
   + the **instance type** you’ll need depends on what size Shadow network you’ll want to simulate (see [the shadow-plugin-tor wiki](https://github.com/shadow/shadow-plugin-tor) for scalability estimates)
   + create and download a new **keypair** if you don’t already have one, since you’ll need it for SSH access
   + create a new **security group** for the Shadow-cloud server
   + configure the **firewall** to allow inbound SSH on 0.0.0.0/0
1. Once the instance is launched and ready, find the public DNS info and remotely log into the machine using the keypair you downloaded:
```bash
ssh -i your-key.pem ubuntu@your-public-dns.amazonaws.com
```
1. Once logged in, view `~/README` and `~/shadow-git-clone/README` for more information