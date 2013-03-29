1. **Shadow is running at 100% CPU. Is that normal?**  
Yes. Shadow always runs at 100% CPU because its continuously processing simulation events as fast as possible. All other things constant, an experiment will finish quicker with a faster CPU.

2. **I need deterministic runs. But every time I run Shadow with the same configuration file, I get slightly different results.**
You need to use the "--cpu-threshold=-1" flag when running Shadow to disable the CPU model, as it introduces non-determinism into the experiment in exchange for more realistic CPU behaviors. (See also: "shadow --help-all")

3. **Is Shadow multi-threaded?**
Yes. Shadow can run with _N_ worker threads by specifying `-w _N_` or `--workers=_N_` on the command line. Note that virtual nodes depend on network packets that can potentially arrive from other virtual nodes. Therefore, each worker can only advance according to the propagation delay to avoid dependency violations.

4. **Can I use Shadow/Scallion with my custom Tor modifications?**
Yes. You'll need to build Shadow with the `--tor-prefix` option set to the path of your Tor source directory. Then, every time you make Tor modifications, you need to rebuild and reinstall Shadow and Scallion, again using the `--tor-prefix` option.