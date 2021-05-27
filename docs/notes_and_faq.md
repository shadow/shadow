- [Shadow is running at 100% CPU. Is that normal?](#shadow-is-running-at-100-cpu-is-that-normal)
- [Is it possible to achieve deterministic experiments, so that every time I run Shadow with the same configuration file, I get the same results?](#is-it-possible-to-achieve-deterministic-experiments-so-that-every-time-i-run-shadow-with-the-same-configuration-file-i-get-the-same-results)
- [Why don't the consensus values from a v3bw file for the torflowauthority show up in the directory authority's `cached-consenus` file?](#why-dont-the-consensus-values-from-a-v3bw-file-for-the-torflowauthority-show-up-in-the-directory-authoritys-cached-consenus-file)
- [Is Shadow the right tool for my research question?](#is-shadow-the-right-tool-for-my-research-question)

#### Shadow is running at 100% CPU. Is that normal?

Yes. In single-thread mode, Shadow runs at 100% CPU because its continuously processing simulation events as fast as possible. All other things constant, an experiment will finish quicker with a faster CPU. Due to node dependencies, thread CPU utilization will be less than 100% in multi-thread mode.

#### Is it possible to achieve deterministic experiments, so that every time I run Shadow with the same configuration file, I get the same results?

Todo!

#### Why don't the consensus values from a v3bw file for the torflowauthority show up in the directory authority's `cached-consenus` file?

Tor currently requires 3 directory authorities to be configured in order to accept values from a v3bw file; otherwise the directory authorities use relays' advertised bandwidth when creating the consensus and the v3bw file entries are ignored.

#### Is Shadow the right tool for my research question?

Shadow is a network simulator/emulator hybrid. It runs real applications, but it simulates network and system functions thereby emulating the kernel to the application. The suitability of Shadow to your problem depends upon what exactly you are trying to measure. If you are interested in analyzing changes in application behavior, e.g. application layer queuing, failure modes, or design changes, and how those changes affect the operation of the system and  network performance, then Shadow seems like a very good choice (especially if you want to minimize work on your end). If your research relies on, e.g., the accuracy of specific kernel features or kernel parameter settings, or dynamic changes in Internet routing, then Shadow may not be the right choice as it does not precisely model these behaviors. Shadow is also not the best at measuring cryptographic overhead, so if that is desired then it should probably be done more directly as a separate research component.
