use std::borrow::Borrow;
use std::ffi::{CStr, OsStr};
use std::io::IsTerminal;
use std::os::unix::ffi::OsStrExt;
use std::thread;

use anyhow::{self, Context};
use clap::Parser;
use nix::sys::{personality, resource, signal};
use signal_hook::{consts, iterator::Signals};

use crate::core::controller::Controller;
use crate::core::logger::shadow_logger;
use crate::core::sim_config::SimConfig;
use crate::core::support::configuration::{CliOptions, ConfigFileOptions, ConfigOptions};
use crate::core::worker;
use crate::cshadow as c;
use crate::utility::shm_cleanup;

fn verify_supported_system() -> anyhow::Result<()> {
    let uts_name = nix::sys::utsname::uname()?;
    let sysname = uts_name
        .sysname()
        .to_str()
        .with_context(|| "Decoding system name")?;
    if sysname != "Linux" {
        anyhow::bail!("Unsupported sysname: {sysname}");
    }
    let version = uts_name
        .release()
        .to_str()
        .with_context(|| "Decoding system release")?;
    let mut version_parts = version.split('.');
    let Some(major) = version_parts.next() else {
        anyhow::bail!("Couldn't find major version in : {version}");
    };
    let major: i32 = major
        .parse()
        .with_context(|| format!("Parsing major version number '{major}'"))?;
    let Some(minor) = version_parts.next() else {
        anyhow::bail!("Couldn't find minor version in : {version}");
    };
    let minor: i32 = minor
        .parse()
        .with_context(|| format!("Parsing minor version number '{minor}'"))?;

    // Keep in sync with `supported_platforms.md`.
    const MIN_KERNEL_VERSION: (i32, i32) = (5, 4);

    if (major, minor) < MIN_KERNEL_VERSION {
        anyhow::bail!(
            "kernel version {major}.{minor} is older than minimum supported version {}.{}",
            MIN_KERNEL_VERSION.0,
            MIN_KERNEL_VERSION.1
        );
    }

    Ok(())
}

/// Main entry point for the simulator.
pub fn run_shadow(build_info: &ShadowBuildInfo, args: Vec<&OsStr>) -> anyhow::Result<()> {
    // Install the shared memory allocator's clean up routine on exit. Once this guard is dropped,
    // all shared memory allocations will become invalid.
    let _guard = unsafe { crate::shadow_shmem::allocator::SharedMemAllocatorDropGuard::new() };

    let version_display = option_env!("SHADOW_GIT_VERSION").unwrap_or(env!("CARGO_PKG_VERSION"));

    if unsafe { c::main_checkGlibVersion() } != 0 {
        return Err(anyhow::anyhow!("Unsupported GLib version"));
    }

    let mut signals_list = Signals::new([consts::signal::SIGINT, consts::signal::SIGTERM])?;
    thread::spawn(move || {
        // `next()` should block until we've received a signal, or `signals_list` is closed and
        // `None` is returned
        if let Some(signal) = signals_list.forever().next() {
            log::info!("Received signal {}. Flushing log and exiting", signal);
            log::logger().flush();
            std::process::exit(1);
        }
        log::debug!("Finished waiting for a signal");
    });

    // unblock all signals in shadow and child processes since cmake's ctest blocks
    // SIGTERM (and maybe others)
    signal::sigprocmask(
        signal::SigmaskHow::SIG_SETMASK,
        Some(&signal::SigSet::empty()),
        None,
    )?;

    // parse the options from the command line
    let options = match CliOptions::try_parse_from(args.clone()) {
        Ok(x) => x,
        Err(e) => {
            // will print to either stdout or stderr with formatting
            e.print().unwrap();
            if e.use_stderr() {
                // the `clap::Error` represents an error (ex: invalid flag)
                std::process::exit(1);
            } else {
                // the `clap::Error` represents a non-error, but we'll want to exit anyways (ex:
                // '--help')
                std::process::exit(0);
            }
        }
    };

    if options.show_build_info {
        eprintln!("Shadow {version_display}");
        unsafe { c::main_printBuildInfo(build_info) };
        std::process::exit(0);
    }

    if options.shm_cleanup {
        // clean up any orphaned shared memory
        shm_cleanup::shm_cleanup(shm_cleanup::SHM_DIR_PATH)
            .context("Cleaning shared memory files")?;
        std::process::exit(0);
    }

    // read from stdin if the config filename is given as '-'
    let config_filename: String = match options.config.as_ref().unwrap().as_str() {
        "-" => "/dev/stdin",
        x => x,
    }
    .into();

    // load the configuration yaml
    let config_file = load_config_file(&config_filename, true)
        .with_context(|| format!("Failed to load configuration file {}", config_filename))?;

    // generate the final shadow configuration from the config file and cli options
    let shadow_config = ConfigOptions::new(config_file, options.clone());

    if options.show_config {
        eprintln!("{:#?}", shadow_config);
        return Ok(());
    }

    // configure other global state
    if shadow_config.experimental.use_object_counters.unwrap() {
        worker::enable_object_counters();
    }

    // get the log level
    let log_level = shadow_config.general.log_level.unwrap();
    let log_level: log::Level = log_level.into();

    // start up the logging subsystem to handle all future messages
    let log_errors_to_stderr = shadow_config.experimental.log_errors_to_tty.unwrap()
        && !std::io::stdout().lock().is_terminal()
        && std::io::stderr().lock().is_terminal();
    shadow_logger::init(log_level.to_level_filter(), log_errors_to_stderr).unwrap();

    // disable log buffering during startup so that we see every message immediately in the terminal
    shadow_logger::set_buffering_enabled(false);

    // check if some log levels have been compiled out
    if log_level > log::STATIC_MAX_LEVEL {
        log::warn!(
            "Log level set to {}, but messages higher than {} have been compiled out",
            log_level,
            log::STATIC_MAX_LEVEL,
        );
    }

    // warn if running with root privileges
    if nix::unistd::getuid().is_root() {
        // a real-world example is opentracker, which will attempt to drop privileges if it detects
        // that the effective user is root, but this fails in shadow and opentracker exits with an
        // error
        log::warn!(
            "Shadow is running as root. Shadow does not emulate Linux permissions, and some
            applications may behave differently when running as root. It is recommended to run
            Shadow as a non-root user."
        );
    } else if nix::unistd::geteuid().is_root() {
        log::warn!(
            "Shadow is running with root privileges. Shadow does not emulate Linux permissions,
            and some applications may behave differently when running with root privileges. It
            is recommended to run Shadow as a non-root user."
        );
    }

    // before we run the simulation, clean up any orphaned shared memory
    if let Err(e) = shm_cleanup::shm_cleanup(shm_cleanup::SHM_DIR_PATH) {
        log::warn!("Unable to clean up shared memory files: {:?}", e);
    }

    // save the platform data required for CPU pinning
    if shadow_config.experimental.use_cpu_pinning.unwrap() {
        #[allow(clippy::collapsible_if)]
        if unsafe { c::affinity_initPlatformInfo() } != 0 {
            return Err(anyhow::anyhow!("Unable to initialize platform info"));
        }
    }

    // raise fd soft limit to hard limit
    raise_rlimit(resource::Resource::RLIMIT_NOFILE).context("Could not raise fd limit")?;

    // raise number of processes/threads soft limit to hard limit
    raise_rlimit(resource::Resource::RLIMIT_NPROC).context("Could not raise proc limit")?;

    if shadow_config.experimental.use_sched_fifo.unwrap() {
        set_sched_fifo().context("Could not set real-time scheduler mode to SCHED_FIFO")?;
        log::debug!("Successfully set real-time scheduler mode to SCHED_FIFO");
    }

    // Disable address space layout randomization of processes forked from this
    // one to improve determinism in cases when an executable under simulation
    // branch on memory addresses.
    match disable_aslr() {
        Ok(()) => log::debug!("ASLR disabled for processes forked from this parent process"),
        Err(e) => log::warn!("Could not disable address space layout randomization. This may affect determinism: {:?}", e),
    };

    // check sidechannel mitigations
    if sidechannel_mitigations_enabled().context("Failed to get sidechannel mitigation status")? {
        log::warn!(
            "Speculative Store Bypass sidechannel mitigation is enabled (perhaps by seccomp?). \
             This typically adds ~30% performance overhead."
        );
    }

    // log some information
    log::info!("Starting Shadow {version_display}");
    unsafe { c::main_logBuildInfo(build_info) };
    log_environment(args.clone());

    if let Err(e) = verify_supported_system() {
        log::warn!("Couldn't verify supported system: {e:?}")
    }

    log::debug!("Startup checks passed, we are ready to start the simulation");

    // allow gdb to attach before starting the simulation
    if options.gdb {
        pause_for_gdb_attach().context("Could not pause shadow to allow gdb to attach")?;
    }

    let sim_config = SimConfig::new(&shadow_config, &options.debug_hosts.unwrap_or_default())
        .context("Failed to initialize the simulation")?;

    // allocate and initialize our main simulation driver
    let controller = Controller::new(sim_config, &shadow_config);

    // enable log buffering if not at trace level
    let buffer_log = !log::log_enabled!(log::Level::Trace);
    shadow_logger::set_buffering_enabled(buffer_log);
    if buffer_log {
        log::info!("Log message buffering is enabled for efficiency");
    }

    // run the simulation
    controller.run().context("Failed to run the simulation")?;

    // disable log buffering
    shadow_logger::set_buffering_enabled(false);
    if buffer_log {
        // only show if we disabled buffering above
        log::info!("Log message buffering is disabled during cleanup");
    }

    Ok(())
}

fn load_config_file(
    filename: impl AsRef<std::path::Path>,
    extended_yaml: bool,
) -> anyhow::Result<ConfigFileOptions> {
    let file = std::fs::File::open(filename).context("Could not open config file")?;

    // serde's default behaviour is to silently ignore duplicate keys during deserialization so we
    // would typically need to use serde_with's `maps_duplicate_key_is_error()` on our
    // 'ConfigFileOptions' struct to prevent duplicate hostnames, but since we deserialize to
    // serde_yaml's `Value` type initially we don't need to prevent duplicate keys as serde_yaml
    // does this for us: https://github.com/dtolnay/serde-yaml/pull/301

    let mut config_file: serde_yaml::Value =
        serde_yaml::from_reader(file).context("Could not parse configuration file as yaml")?;

    if extended_yaml {
        // apply the merge before removing extension fields
        config_file
            .apply_merge()
            .context("Could not merge '<<' keys")?;

        // remove top-level extension fields
        if let serde_yaml::Value::Mapping(ref mut mapping) = &mut config_file {
            // remove entries having a key beginning with "x-" (follows docker's convention:
            // https://docs.docker.com/compose/compose-file/#extension)
            mapping.retain(|key, _value| {
                if let serde_yaml::Value::String(key) = key {
                    if key.starts_with("x-") {
                        return false;
                    }
                }
                true
            });
        }
    }

    serde_yaml::from_value(config_file).context("Could not parse configuration file")
}

fn pause_for_gdb_attach() -> anyhow::Result<()> {
    let pid = nix::unistd::getpid();
    log::info!(
        "Pausing with SIGTSTP to enable debugger attachment (pid {})",
        pid
    );
    eprintln!(
        "** Pausing with SIGTSTP to enable debugger attachment (pid {})",
        pid
    );

    signal::raise(signal::Signal::SIGTSTP)?;

    log::info!("Resuming now");
    Ok(())
}

fn set_sched_fifo() -> anyhow::Result<()> {
    let mut param: libc::sched_param = unsafe { std::mem::zeroed() };
    param.sched_priority = 1;

    let rv = nix::errno::Errno::result(unsafe {
        libc::sched_setscheduler(0, libc::SCHED_FIFO, &param as *const _)
    })
    .context("Could not set kernel SCHED_FIFO")?;

    assert_eq!(rv, 0);

    Ok(())
}

fn raise_rlimit(resource: resource::Resource) -> anyhow::Result<()> {
    let (_soft_limit, hard_limit) = resource::getrlimit(resource)?;
    resource::setrlimit(resource, hard_limit, hard_limit)?;
    Ok(())
}

fn disable_aslr() -> anyhow::Result<()> {
    let pers = personality::get()?;
    personality::set(pers | personality::Persona::ADDR_NO_RANDOMIZE)?;
    Ok(())
}

fn sidechannel_mitigations_enabled() -> anyhow::Result<bool> {
    let state = nix::errno::Errno::result(unsafe {
        libc::prctl(
            libc::PR_GET_SPECULATION_CTRL,
            libc::PR_SPEC_STORE_BYPASS,
            0,
            0,
            0,
        )
    })
    .context("Failed prctl()")?;
    let state = state as u32;
    Ok((state & libc::PR_SPEC_DISABLE) != 0)
}

fn log_environment(args: Vec<&OsStr>) {
    for arg in args {
        log::info!("arg: {}", arg.to_string_lossy());
    }

    for (key, value) in std::env::vars_os() {
        let level = match key.to_string_lossy().borrow() {
            "LD_PRELOAD" | "LD_STATIC_TLS_EXTRA" | "G_DEBUG" | "G_SLICE" => log::Level::Info,
            _ => log::Level::Trace,
        };
        log::log!(level, "env: {:?}={:?}", key, value);
    }
}

#[repr(C)]
pub struct ShadowBuildInfo {
    build: *const libc::c_char,
    info: *const libc::c_char,
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C-unwind" fn main_runShadow(
        build_info: *const ShadowBuildInfo,
        argc: libc::c_int,
        argv: *const *const libc::c_char,
    ) -> libc::c_int {
        let args = (0..argc).map(|x| unsafe { CStr::from_ptr(*argv.add(x as usize)) });
        let args = args.map(|x| OsStr::from_bytes(x.to_bytes()));
        let build_info = unsafe { build_info.as_ref().unwrap() };

        let result = run_shadow(build_info, args.collect());
        log::logger().flush();

        if let Err(e) = result {
            // log the full error, its context, and its backtrace if enabled
            if log::log_enabled!(log::Level::Error) {
                for line in format!("{:?}", e).split('\n') {
                    log::error!("{}", line);
                }
                log::logger().flush();

                // print the short error
                eprintln!("** Shadow did not complete successfully: {}", e);
                eprintln!("**   {}", e.root_cause());
                eprintln!("** See the log for details");
            } else {
                // logging may not be configured yet, so print to stderr
                eprintln!("{:?}", e);
            }

            return 1;
        }

        eprintln!("** Shadow completed successfully");
        0
    }
}
