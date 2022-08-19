use std::sync::atomic::AtomicU32;
use std::sync::Arc;
use std::time::Duration;

use anyhow::Context;
use rand::Rng;
use rand::SeedableRng;
use rand_xoshiro::Xoshiro256PlusPlus;

use crate::core::manager::Manager;
use crate::core::scheduler::runahead::Runahead;
use crate::core::sim_config::SimConfig;
use crate::core::support::configuration::{ConfigOptions, Flatten};
use crate::core::support::emulated_time::EmulatedTime;
use crate::core::support::simulation_time::SimulationTime;
use crate::core::worker;
use crate::cshadow as c;
use crate::utility::status_bar::{self, StatusBar, StatusPrinter};
use crate::utility::time::TimeParts;
use crate::utility::SyncSendPointer;

pub struct Controller<'a> {
    // general options and user configuration for the simulation
    config: &'a ConfigOptions,
    sim_config: Option<SimConfig>,

    // calculates the runahead for the next simulation round
    runahead: Runahead,

    // the simulator should attempt to end immediately after this time
    end_time: EmulatedTime,
}

impl<'a> Controller<'a> {
    pub fn new(sim_config: SimConfig, config: &'a ConfigOptions) -> Self {
        let min_runahead_config: Option<Duration> =
            config.experimental.runahead.flatten().map(|x| x.into());
        let min_runahead_config: Option<SimulationTime> =
            min_runahead_config.map(|x| x.try_into().unwrap());

        let end_time: Duration = config.general.stop_time.unwrap().into();
        let end_time: SimulationTime = end_time.try_into().unwrap();
        let end_time = EmulatedTime::SIMULATION_START + end_time;

        let smallest_latency =
            SimulationTime::from_nanos(sim_config.routing_info.get_smallest_latency_ns().unwrap());

        Self {
            config,
            sim_config: Some(sim_config),
            runahead: Runahead::new(
                config.experimental.use_dynamic_runahead.unwrap(),
                smallest_latency,
                min_runahead_config,
            ),
            end_time,
        }
    }

    pub fn run(mut self) -> anyhow::Result<()> {
        let mut sim_config = self.sim_config.take().unwrap();

        let status_logger = self.config.general.progress.unwrap().then(|| {
            let state = ShadowStatusBarState::new(self.end_time);

            if nix::unistd::isatty(libc::STDERR_FILENO).unwrap() {
                let redraw_interval = Duration::from_millis(1000);
                StatusLogger::Bar(StatusBar::new(state, redraw_interval))
            } else {
                StatusLogger::Printer(StatusPrinter::new(state))
            }
        });

        let dns = unsafe { c::dns_new() };
        assert!(!dns.is_null());

        // set the simulation's global state
        worker::WORKER_SHARED
            .set(worker::WorkerShared {
                ip_assignment: sim_config.ip_assignment,
                routing_info: sim_config.routing_info,
                host_bandwidths: sim_config.host_bandwidths,
                // safe since the DNS type has an internal mutex, and since global memory is leaked
                // we don't ever need to free this
                dns: unsafe { SyncSendPointer::new(dns) },
                num_plugin_errors: AtomicU32::new(0),
                // allow the status logger's state to be updated from anywhere
                status_logger_state: status_logger.as_ref().map(|x| Arc::clone(x.status())),
            })
            .expect("The global state has already been set during the program's execution");

        let manager_hosts = std::mem::take(&mut sim_config.hosts);
        let manager_rand = Xoshiro256PlusPlus::seed_from_u64(sim_config.random.gen());
        let manager = Manager::new(
            &self,
            self.config,
            manager_hosts,
            self.end_time,
            manager_rand,
        )
        .context("Failed to initialize the manager")?;

        log::info!("Running simulation");
        manager.run()?;
        log::info!("Finished simulation");

        let num_plugin_errors = worker::WORKER_SHARED.get().unwrap().plugin_error_count();
        if num_plugin_errors > 0 {
            return Err(anyhow::anyhow!(
                "{num_plugin_errors} managed processes exited with a non-zero error code"
            ));
        }

        Ok(())
    }
}

/// Controller methods that are accessed by the manager.
pub trait SimController {
    fn manager_finished_current_round(
        &self,
        min_next_event_time: EmulatedTime,
    ) -> Option<(EmulatedTime, EmulatedTime)>;
    fn update_min_runahead(&self, min_path_latency: SimulationTime);
}

impl SimController for Controller<'_> {
    fn manager_finished_current_round(
        &self,
        min_next_event_time: EmulatedTime,
    ) -> Option<(EmulatedTime, EmulatedTime)> {
        // TODO: once we get multiple managers, we have to block them here until they have all
        // notified us that they are finished

        let runahead = self.runahead.get();
        assert_ne!(runahead, SimulationTime::ZERO);

        let new_start = min_next_event_time;

        // update the new window end as one interval past the new window start, making sure we don't
        // run over the experiment end time
        let new_end = new_start.checked_add(runahead).unwrap_or(EmulatedTime::MAX);
        let new_end = std::cmp::min(new_end, self.end_time);

        let continue_running = new_start < new_end;
        continue_running.then(|| (new_start, new_end))
    }

    fn update_min_runahead(&self, min_path_latency: SimulationTime) {
        self.runahead.update_lowest_used_latency(min_path_latency);
    }
}

#[derive(Debug)]
pub struct ShadowStatusBarState {
    start: std::time::Instant,
    pub current: EmulatedTime,
    end: EmulatedTime,
    pub num_failed_processes: u32,
}

impl std::fmt::Display for ShadowStatusBarState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let sim_current = self.current.duration_since(&EmulatedTime::SIMULATION_START);
        let sim_end = self.end.duration_since(&EmulatedTime::SIMULATION_START);
        let frac = sim_current.as_millis() as f32 / sim_end.as_millis() as f32;

        let sim_current = TimeParts::from_nanos(sim_current.as_nanos().into());
        let sim_end = TimeParts::from_nanos(sim_end.as_nanos().into());
        let realtime = TimeParts::from_nanos(self.start.elapsed().as_nanos());

        write!(
            f,
            "{}% — simulated: {}/{}, realtime: {}, processes failed: {}",
            (frac * 100.0).round() as i8,
            sim_current.fmt_hr_min_sec_milli(),
            sim_end.fmt_hr_min_sec(),
            realtime.fmt_hr_min_sec(),
            self.num_failed_processes,
        )
    }
}

impl ShadowStatusBarState {
    pub fn new(end: EmulatedTime) -> Self {
        Self {
            start: std::time::Instant::now(),
            current: EmulatedTime::SIMULATION_START,
            end,
            num_failed_processes: 0,
        }
    }
}

enum StatusLogger<T: 'static + status_bar::StatusBarState> {
    Printer(StatusPrinter<T>),
    Bar(StatusBar<T>),
}

impl<T: 'static + status_bar::StatusBarState> StatusLogger<T> {
    pub fn status(&self) -> &Arc<status_bar::Status<T>> {
        match self {
            Self::Printer(x) => x.status(),
            Self::Bar(x) => x.status(),
        }
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn controller_updateMinRunahead(
        controller: *const Controller,
        min_path_latency: c::SimulationTime,
    ) {
        let controller = unsafe { controller.as_ref() }.unwrap();
        let min_path_latency = SimulationTime::from_c_simtime(min_path_latency).unwrap();

        controller.update_min_runahead(min_path_latency);
    }
}
