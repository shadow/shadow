**NOTE** - _This page is incomplete_

The `resource/scallion-hosts` directory contains sample network configurations to get started with some Tor experiments. The maximum memory requirements are given below. Note that the smallest possible EC2 instance required to run these configurations are also given, but the memory requirement guidelines hold for any machine.

The memory requirements of the included topologies, and the EC2 instances supporting those memory requirements are given below. For instructions on running Shadow in the cloud, visit [[Running Shadow on EC2]].

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