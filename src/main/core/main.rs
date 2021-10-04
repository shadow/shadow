use std::borrow::Borrow;
use std::ffi::{CStr, OsStr};
use std::os::unix::ffi::OsStrExt;

use anyhow::{self, Context};
use clap::Clap;
use nix::sys::{personality, resource, signal};

use crate::core::logger::log_wrapper::c_to_rust_log_level;
use crate::core::logger::shadow_logger;
use crate::core::support::configuration::{CliOptions, ConfigFileOptions, ConfigOptions};
use crate::cshadow as c;

/// Main entry point for the simulator.
pub fn run_shadow<'a>(args: Vec<&'a OsStr>) -> anyhow::Result<()> {
    if unsafe { c::main_checkGlibVersion() } != 0 {
        return Err(anyhow::anyhow!("Unsupported GLib version"));
    }

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
            if e.use_stderr() {
                eprint!("{}", e);
                std::process::exit(1);
            } else {
                print!("{}", e);
                std::process::exit(0);
            }
        }
    };

    if options.show_build_info {
        unsafe { c::main_printBuildInfo() };
        std::process::exit(0);
    }

    if options.shm_cleanup {
        // clean up any orphaned shared memory
        unsafe { c::shmemcleanup_tryCleanup() };
        std::process::exit(0);
    }

    // read from stdin if the config filename is given as '-'
    let config_filename: String = match options.config.as_ref().unwrap().as_str() {
        "-" => "/dev/stdin",
        x => x,
    }
    .into();

    // load the configuration yaml
    let file = std::fs::File::open(&config_filename)
        .context(format!("Could not open config file {:?}", &config_filename))?;
    let config_file: ConfigFileOptions = serde_yaml::from_reader(file).context(format!(
        "Could not parse configuration file {:?}",
        &config_filename
    ))?;

    // generate the final shadow configuration from the config file and cli options
    let config = ConfigOptions::new(config_file, options.clone());

    if options.show_config {
        eprintln!("{:#?}", config);
        return Ok(());
    }

    // run any global C configuration handlers
    unsafe { c::runConfigHandlers(&config as *const ConfigOptions) };

    // disable log buffering during startup so that we see every message immediately in the terminal
    shadow_logger::init().unwrap();
    shadow_logger::set_buffering_enabled(false);

    // start up the logging subsystem to handle all future messages
    let log_level = config.general.log_level.as_ref().unwrap().to_c_loglevel();
    unsafe { log_bindings::logger_setDefault(c::rustlogger_new()) };
    unsafe { log_bindings::logger_setLevel(log_bindings::logger_getDefault(), log_level) };

    // check if some log levels have been compiled out
    let log_level = c_to_rust_log_level(log_level).unwrap();
    if log_level > log::STATIC_MAX_LEVEL {
        log::warn!(
            "Log level set to {}, but messages higher than {} have been compiled out",
            log_level,
            log::STATIC_MAX_LEVEL,
        );
    }

    // before we run the simulation, clean up any orphaned shared memory
    unsafe { c::shmemcleanup_tryCleanup() };

    // save the platform data required for CPU pinning
    if config.experimental.use_cpu_pinning.unwrap() {
        if unsafe { c::affinity_initPlatformInfo() } != 0 {
            return Err(anyhow::anyhow!("Unable to initialize platform info"));
        }
    }

    // raise fd soft limit to hard limit
    raise_rlimit(resource::Resource::RLIMIT_NOFILE).context("Could not raise fd limit")?;

    // raise number of processes/threads soft limit to hard limit
    raise_rlimit(resource::Resource::RLIMIT_NPROC).context("Could not raise proc limit")?;

    if config.experimental.use_sched_fifo.unwrap() {
        set_sched_fifo().context("Could not set real-time scheduler mode to SCHED_FIFO")?;
        log::debug!("Successfully set real-time scheduler mode to SCHED_FIFO");
    }

    // Disable address space layout randomization of processes forked from this
    // one to improve determinism in cases when an executable under simulation
    // branch on memory addresses.
    disable_aslr().context("Could not disable plugin address space layout randomization")?;
    log::debug!("ASLR disabled for processes forked from this parent process");

    // check sidechannel mitigations
    if unsafe { c::main_sidechannelMitigationsEnabled() } {
        log::warn!(
            "Speculative Store Bypass sidechannel mitigation is enabled (perhaps by seccomp?). \
             This typically adds ~30% performance overhead."
        );
    }

    // log some information
    unsafe { c::main_logBuildInfo() };
    log_environment(args.clone());

    log::debug!("Startup checks passed, we are ready to start the simulation");

    // allow gdb to attach before starting the simulation
    if options.gdb {
        pause_for_gdb_attach().context("Could not pause shadow to allow gdb to attach")?;
    }

    // allocate and initialize our main simulation driver
    let controller = unsafe { c::controller_new(&config as *const _) };
    assert!(!controller.is_null());

    // run the simulation
    let rv = unsafe { c::controller_run(controller) };
    unsafe { c::controller_free(controller) };
    std::mem::drop(controller);

    if rv != 0 {
        return Err(anyhow::anyhow!("Controller exited with code {}", rv));
    }

    Ok(())
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

    if unsafe { libc::sched_setscheduler(0, libc::SCHED_FIFO, &param as *const _) } != 0 {
        return Err(anyhow::anyhow!(
            "Could not set kernel SCHED_FIFO: {}",
            nix::errno::Errno::from_i32(nix::errno::errno()),
        ));
    }

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

fn log_environment<'a>(args: Vec<&'a OsStr>) {
    for arg in args {
        log::info!("arg: {}", arg.to_string_lossy());
    }

    for (key, value) in std::env::vars_os() {
        let level = match key.to_string_lossy().borrow() {
            "LD_PRELOAD" | "SHADOW_SPAWNED" | "LD_STATIC_TLS_EXTRA" | "G_DEBUG" | "G_SLICE" => {
                log::Level::Info
            }
            _ => log::Level::Trace,
        };
        log::log!(level, "env: {:?}={:?}", key, value);
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn main_runShadow(
        argc: libc::c_int,
        argv: *const *const libc::c_char,
    ) -> libc::c_int {
        let args = (0..argc).map(|x| unsafe { CStr::from_ptr(*argv.add(x as usize)) });
        let args = args.map(|x| OsStr::from_bytes(x.to_bytes()));

        let result = run_shadow(args.collect());
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
                eprintln!("** See the log for details");
            } else {
                eprintln!("{:?}", e);
            }

            return 1;
        }

        eprintln!("** Shadow completed successfully");
        0
    }
}
