To customize a simulation, you'll want to configure Shadow, the network model, and/or the behavior of the traffic generator:

  + Build your own [Shadow Config][3.1-Shadow-Config] file to configure the hosts that Shadow should run, including network placement, bandwidth, and the software that they execute.
  + Build your own [Network Config][3.1-Network-Config] file to configure the network topology graph to which hosts can be connected, including network latency, jitter, and packet loss rates.
  + Build your own [TGen Config][3.1-TGen-Config] file to configure the data transfer amounts and the generator behaviors, including parallel downloads, pause times, and download frequency.