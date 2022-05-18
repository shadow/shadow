# Disabling Sidechannel Mitigations

Sidechannel attacks in the style of
[Spectre](https://en.wikipedia.org/wiki/Spectre_(security_vulnerability)) and
[Meltdown](https://en.wikipedia.org/wiki/Meltdown_(security_vulnerability))
allow malicious code to access data it otherwise wouldn't be able to. Modern
systems employ countermeasures to prevent these attacks, which typically incur
some performance cost, and may not be necessary when running Shadow simulations.
i.e. Shadow's performance can be improved by disabling these mitigations.

Keep in mind that Shadow already isn't designed to protect itself or its host
system from malicious software. See [Security](security.md).

## Speculative Store Bypass

The Speculative Store Bypass attack allows malicious code to read data it
otherwise wouldn't be able to, e.g. due to software sandboxing such as in a
javascript engine.  For a high-level overview of this attack and mitigations,
see:
<https://www.redhat.com/en/blog/speculative-store-bypass-explained-what-it-how-it-works>.
For a more technical overview, see 
<https://software.intel.com/content/dam/develop/external/us/en/documents/336996-speculative-execution-side-channel-mitigations.pdf>.

We have observed the mitigation for this vulnerability to add roughly a [30%
performance overhead](https://github.com/shadow/shadow/issues/1489#issuecomment-871445482) to Shadow simulations. Because process isolation is
already sufficient to mitigate this vulnerability (See ["Process
Isolation"](https://software.intel.com/content/dam/develop/external/us/en/documents/336996-speculative-execution-side-channel-mitigations.pdf)),
and because Shadow already makes [no attempt](security.md) to protect itself
from malicious code within its own processes, and isn't designed to run in a
managed-code environment itself, enabling this mitigation in Shadow and its
managed processes doesn't have any clear benefit.

Shadow itself makes use of `seccomp`, but uses the
`SECCOMP_FILTER_FLAG_SPEC_ALLOW` flag to avoid turning on this mitigation.  It
also logs a warning if it detects this mitigation is already enabled.

One common way this mitigation can be turned on inadvertently is by running
inside a Docker container, with seccomp enabled (which is the default).  You can
avoid this by turning off seccomp entirely (using [`--security-opt
seccomp=unconfined`](https://docs.docker.com/engine/security/seccomp/#run-without-the-default-seccomp-profile), but this might not be an option when running in a
shared environment. Unfortunately, Docker currently [doesn't
expose](https://github.com/moby/moby/issues/42619) an option to use its seccomp
functionality without turning on this mitigation. 

Another way to avoid enabling this mitigation is by changing the [kernel
parameter](
https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)
`spec_store_bypass_disable`. Overriding its default value of `seccomp` to
`prctl` will still allow software sandboxes such as javascript engines to enable
this mitigation, but will no longer enable it by default when installing a
`seccomp` filter. In principle this could create a vulnerability if there's code
running on the system that relies on the default behavior without explicitly
opting in via `prctl`, so use some caution. For more discussion on this
parameter, see this discussion on the kernel mailing list about whether the
kernel default ought to be changed from `seccomp` to `prctl`:
<https://lore.kernel.org/lkml/20201104215702.GG24993@redhat.com/>

## Other mitigations

In some ad-hoc measurements we've found that disabling *all* sidechannel
mitigations with
[`mitigations=off`](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)
also provides a significant performance boost. We haven't thoroughly evaluated
the exact benefits though, and this setting could expose your system to attack.
At a minimum, this isn't advised on a system that runs *any* untrusted code at
*any* privilege level, including in managed environments such as running
javascript in a web browser.
