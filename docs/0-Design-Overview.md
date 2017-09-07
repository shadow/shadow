## Mission: Run Tor in a Box

<!--[[https://raw.githubusercontent.com/wiki/shadow/shadow/assets/torinabox.png|align=right|width=175px]]-->
<!--[Run Tor in a box with Shadow!][image-torinabox]-->
<!--[image-torinabox]: https://raw.githubusercontent.com/wiki/shadow/shadow/assets/torinabox.png-->

<a href="https://raw.githubusercontent.com/wiki/shadow/shadow/assets/torinabox.png"><img align="right" width="225" alt="Run Tor in a box with Shadow!" src="https://raw.githubusercontent.com/wiki/shadow/shadow/assets/torinabox.png"></a>

Shadow was developed because there was a recognized need for an accurate, efficient, and scalable tool for Tor experimentation: using the PlanetLab platform is undesirable due to management overhead and lack of control; existing emulators are far too inefficient when scaling to thousands of nodes; roll-your-own simulators are often too inaccurate or generic to be useful for multiple projects; and experiments on the live Tor network are often infeasible due to privacy risks.

Our goal was to provide a tool that can be used by anyone with a Linux machine or access to EC2 to hasten the development of research prototypes and reduce the time to deployment. Although originally written with Tor experimentation in mind, Shadow can also run Bitcoin and is useful for researching or prototyping other distributed or peer-to-peer systems including multi-party computation protocols.

## Feature Overview

Shadow does the following:

 + creates an isolated simulation environment where virtual hosts may communicate with each other but not with the Internet
 + natively executes **real applications** like Tor and Bitcoin 
 + provides efficient, accurate, and **controlled** experiments
 + models network topology, latency, and bandwidth
 + runs without root on a single Linux machine, or [in the cloud][wiki-ec2]
 + simulates multiple virtual hosts in virtual time
 + simulates the network (TCP stack) and CPU processing delays
 + can run private Tor networks with user/traffic models based on [Tor metrics][tormetrics] 
 + much, much more!

## Real Applications as Shadow Plug-ins

Shadow is a discrete-event simulator that runs **real applications** like [Tor][tor] . Shadow links to real application software and **natively executes the application code** during simulation, providing faithful experiments and accurate results. Shadow models and runs distributed networks using these applications on a single Linux machine, easing experiment management while keeping the focus on the results.

#### What are Plug-ins?

Plug-ins are shared library shims that are linked to real applications. Shadow dynamically loads these libraries to natively execute the application code. Shadow intercepts a selective set of system calls to enable seamless integration of an application to the simulated environment. In this way, the application may be unaware that it is running in the simulator and will function as if it was running in a standard UNIX environment.

Visit [this page on the wiki][wiki-custom-plugin] for more information about how to write your own custom Shadow plug-in.

#### What is shadow-plugin-tor?

shadow-plugin-tor is a Shadow plug-in for simulating the [Tor][tor] anonymity network. shadow-plugin-tor integrates Tor into Shadow by wrapping the [Tor source code][torsource] with the necessary hooks that allow it to communicate with the Shadow simulator, thereby leveraging Shadow's unique functionality to allow rapid prototyping and experimentation of Tor. shadow-plugin-tor also contains scripts that assist in analyzing results, generating Tor topologies, and running experiments using the generated topologies.

Visit [the shadow-plugin-tor wiki page][wiki-scallion] for more information on the Tor plug-in and its memory requirements.

## Simulation Blueprint

The first step to using Shadow is to create a blueprint of an experiment. The format of the blueprint is standard XML. The XML file tells Shadow when it should create each virtual host, what software each virtual host should run. It also specifies the structure of the network topology, and network properties such as link latency, jitter, and packet loss rates. Shadow contains example XML files to help get started!

<a href="https://raw.githubusercontent.com/wiki/shadow/shadow/assets/design1.png"><img title="design1" src="https://raw.githubusercontent.com/wiki/shadow/shadow/assets/design1.png" alt="" width="520" /></a>

_Shadow takes a simulation blueprint as input. This XML file specifies the structure of the topology and the general flow of the experiment. Shadow's event engine initializes hosts using the blueprint, and runs events on their behalf until the simulation is complete._

## Discrete Time Events

Shadow creates several bootstrapping events after extracting the information from the supplied XML blueprint file. Each of these events are executed at a discrete time instant during the experiment. Each of these bootstrapping events will cause the virtual hosts to start executing the specified software, which in turn will spawn additional events for Shadow to process. Shadow tracks the time each virtual host spends processing inside the application, and delays events according to the host's configured virtual CPU speed. Events are continuously executed until the simulation end time.

As applications send data to each other, Shadow packages that data into an internal type and transfers the pointer between various queues. This process involves the use of the main Shadow event queue to transfer the packet events between virtual hosts, and rate-limiting to ensure each host has the desired bandwidth capacity. The following image, courtesy of Steven Murdoch, may help visualize this process:

<a href="https://raw.githubusercontent.com/wiki/shadow/shadow/assets/shadow_packet_flow.pdf"><img title="shadow_packet_flow" src="https://raw.githubusercontent.com/wiki/shadow/shadow/assets/shadow_packet_flow.png" alt="" width="520" /></a>

_End-to-end application data flows through Shadow socket and interface buffers, while the discrete event queue facilitates the transfer of data between virtual hosts._

## Virtual Host Management

Each virtual host in Shadow runs real software, which are linked as Shadow plug-ins. For each instance of the plug-in that a host is configured to run, Shadow loads the plug-in into a private namespace container using a custom loader and `dlmopen()`. The loader takes care to minimize duplicated memory usage on multiple loads of the same plug-in.

With the virtual host's plug-in loaded, execution is then passed to the application by calling the `main()` function using a version of [GNU portable threads (pth)][gnu-pth]. Pth is an application-space non-preemptive priority-based threading library that can jump call stacks to support plug-ins that run multiple threads and block during execution. Shadow runs the plug-in instance until it would block, and then moves on to execute other plug-ins and hosts.

## Function Interposition

Shadow runs real applications that run on regular UNIX-type systems. These applications expect a wide range of kernel libraries to be available for use. For example, sending and receiving data over the network, system time, and device polling are all generally handled by the kernel at some level. These system functions (and others) are intercepted and redirected through Shadow-specific versions. In this way, Shadow provides the ability for hosts to seamlessly communicate with each other over the virtual network without requiring any changes to the application code.

## More information

See [the original Shadow webcast][youtube-shadow-design] for more information about Shadow's original design, and for an explanation of some experiments that utilize this unique architecture. An explanation of recent architecture updates to support blocking system calls (read/write/send/recv/sleep) and applications that spawn threads [can be found here][cset-rpth-slides]. Checkout [Google Scholar](https://scholar.google.com/scholar?oi=bibs&hl=en&cites=12341442653770148265) for research publications that cite Shadow.

<!--<iframe width="420" height="315" src="http://www.youtube-nocookie.com/embed/Tb7m8OdpD8A" frameborder="0" allowfullscreen></iframe>-->

[tor]: https://www.torproject.org/
[tormetrics]: https://metrics.torproject.org/
[torsource]: https://gitweb.torproject.org/tor.git
[wiki-ec2]: https://github.com/shadow/shadow/wiki/1-Installation-and-Setup#shadow-with-cloud-computing
[wiki-scallion]: https://github.com/shadow/shadow-plugin-tor/wiki
[wiki-custom-plugin]: https://github.com/shadow/shadow/wiki/2-Simulation-Execution-and-Analysis#shadow-plug-ins
[youtube-shadow-design]: http://youtu.be/Tb7m8OdpD8A
[cset-rpth-slides]: http://www.robgjansen.com/talks/shadowbitcoin-cset-20150810.pdf
[gnu-pth]: https://www.gnu.org/software/pth/
