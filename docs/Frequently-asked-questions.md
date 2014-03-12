1. **Shadow is running at 100% CPU. Is that normal?**  
Yes. In single-thread mode, Shadow runs at 100% CPU because its continuously processing simulation events as fast as possible. All other things constant, an experiment will finish quicker with a faster CPU. Due to node dependencies, thread CPU utilization will be less than 100% in multi-thread mode.

2. **Is Shadow multi-threaded?**  
Yes. Shadow can run with _N_ worker threads by specifying `-w N` or `--workers=N` on the command line. Note that virtual nodes depend on network packets that can potentially arrive from other virtual nodes. Therefore, each worker can only advance according to the propagation delay to avoid dependency violations.

3. **Is it possible to achieve deterministic experiments, so that every time I run Shadow with the same configuration file, I get the same results?**  
Yes. You need to use the "--cpu-threshold=-1" flag when running Shadow to disable the CPU model, as it introduces non-determinism into the experiment in exchange for more realistic CPU behaviors. (See also: `shadow --help-all`)

4. **Can I use Shadow/Scallion with my custom Tor modifications?**  
Yes. You'll need to build Shadow with the `--tor-prefix` option set to the path of your Tor source directory. Then, every time you make Tor modifications, you need to rebuild and reinstall Shadow and Scallion, again using the `--tor-prefix` option.