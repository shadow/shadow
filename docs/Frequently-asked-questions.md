1. **Shadow is running at 100% CPU. Is that normal?**  
Yes. Shadow always runs at 100% CPU because its continuously processing simulation events as fast as possible. All other things constant, an experiment will finish quicker with a faster CPU.

2. **I need deterministic runs. But every time I run Shadow with the same configuration file, I get slightly different results.**
You need to use the "--cpu-threshold=-1" flag when running Shadow to disable the CPU model, as it introduces non-determinism into the experiment in exchange for more realistic CPU behaviors. (See also: "shadow --help-all")