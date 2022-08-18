use std::sync::atomic::AtomicU32;
use std::sync::Arc;
use std::sync::RwLock;
use std::time::Duration;

use anyhow::Context;
use rand::Rng;
use rand::SeedableRng;
use rand_xoshiro::Xoshiro256PlusPlus;

use crate::core::manager::Manager;
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

    // global network connectivity info
    is_runahead_dynamic: bool,

    // number of plugins that failed with a non-zero exit code
    num_plugin_errors: AtomicU32,

    // logs the status of the simulation
    status_logger: Option<StatusLogger<ShadowStatusBarState>>,

    scheduling_data: RwLock<ControllerScheduling>,
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

        let status_logger = config.general.progress.unwrap().then(|| {
            let state = ShadowStatusBarState::new(end_time);

            if nix::unistd::isatty(libc::STDERR_FILENO).unwrap() {
                let redraw_interval = Duration::from_millis(1000);
                StatusLogger::Bar(StatusBar::new(state, redraw_interval))
            } else {
                StatusLogger::Printer(StatusPrinter::new(state))
            }
        });

        Self {
            is_runahead_dynamic: config.experimental.use_dynamic_runahead.unwrap(),
            config,
            sim_config: Some(sim_config),
            num_plugin_errors: AtomicU32::new(0),
            status_logger,
            scheduling_data: RwLock::new(ControllerScheduling {
                min_runahead_config,
                smallest_latency,
                // we don't know the min runahead yet
                min_runahead: None,
                end_time,
            }),
        }
    }

    pub fn run(mut self) -> anyhow::Result<()> {
        let end_time = self.scheduling_data.read().unwrap().end_time;
        let mut sim_config = self.sim_config.take().unwrap();

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
                // allow the status logger's state to be updated from anywhere
                status_logger_state: self.status_logger.as_ref().map(|x| Arc::clone(x.status())),
            })
            .expect("The global state has already been set during the program's execution");

        let manager_hosts = std::mem::take(&mut sim_config.hosts);
        let manager_rand = Xoshiro256PlusPlus::seed_from_u64(sim_config.random.gen());
        let manager = Manager::new(&self, self.config, manager_hosts, end_time, manager_rand)
            .context("Failed to initialize the manager")?;

        log::info!("Running simulation");
        manager.run()?;
        log::info!("Finished simulation");

        let num_plugin_errors = self
            .num_plugin_errors
            .load(std::sync::atomic::Ordering::SeqCst);
        if num_plugin_errors > 0 {
            return Err(anyhow::anyhow!(
                "{num_plugin_errors} managed processes exited with a non-zero error code"
            ));
        }

        Ok(())
    }
}

impl std::ops::Drop for Controller<'_> {
    fn drop(&mut self) {
        // stop and clear the status logger
        self.status_logger.as_mut().map(|x| x.stop());
    }
}

/// Controller methods that are accessed by the manager.
pub trait SimController {
    fn manager_finished_current_round(
        &self,
        min_next_event_time: EmulatedTime,
    ) -> Option<(EmulatedTime, EmulatedTime)>;
    fn update_min_runahead(&self, min_path_latency: SimulationTime);
    fn increment_plugin_errors(&self);
}

impl SimController for Controller<'_> {
    fn manager_finished_current_round(
        &self,
        min_next_event_time: EmulatedTime,
    ) -> Option<(EmulatedTime, EmulatedTime)> {
        // TODO: once we get multiple managers, we have to block them here until they have all
        // notified us that they are finished

        let scheduling_data = self.scheduling_data.read().unwrap();
        let (new_start, new_end) = scheduling_data.next_interval_window(min_next_event_time);

        // update the status logger
        let display_time = std::cmp::min(new_start, new_end);
        worker::WORKER_SHARED
            .get()
            .unwrap()
            .update_status_logger(|state| {
                state.current = display_time;
            });

        let continue_running = new_start < new_end;
        continue_running.then(|| (new_start, new_end))
    }

    fn update_min_runahead(&self, min_path_latency: SimulationTime) {
        assert!(min_path_latency > SimulationTime::ZERO);

        // if dynamic runahead is disabled, we don't update 'min_runahead'
        if !self.is_runahead_dynamic {
            return;
        }

        // helper function for checking if we should update the min_runahead
        let should_update = |scheduling_data: &ControllerScheduling| {
            if let Some(min_runahead) = scheduling_data.min_runahead {
                if min_path_latency >= min_runahead {
                    return false;
                }
            }
            // true if runahead was never set before, or new latency is smaller than the old latency
            true
        };

        // an initial check with only a read lock
        {
            let scheduling_data = self.scheduling_data.read().unwrap();

            if !should_update(&scheduling_data) {
                return;
            }
        }

        let old_runahead;
        let min_runahead_config;

        // check the same condition again, but with a write lock
        {
            let mut scheduling_data = self.scheduling_data.write().unwrap();

            if !should_update(&scheduling_data) {
                return;
            }

            // cache the values for logging
            old_runahead = scheduling_data.min_runahead;
            min_runahead_config = scheduling_data.min_runahead_config;

            // update the min runahead
            scheduling_data.min_runahead = Some(min_path_latency);
        }

        // these info messages may appear out-of-order in the log
        log::info!(
            "Minimum time runahead for next scheduling round updated from {:?} \
             to {} ns; the minimum config override is {:?} ns",
            old_runahead.map(|x| x.as_nanos()),
            min_path_latency.as_nanos(),
            min_runahead_config.map(|x| x.as_nanos())
        );
    }

    fn increment_plugin_errors(&self) {
        let old_count = self
            .num_plugin_errors
            .fetch_add(1, std::sync::atomic::Ordering::SeqCst);

        worker::WORKER_SHARED.get().unwrap().update_status_logger(|state| {
            // there is a race condition here, so use the max
            let new_value = old_count + 1;
            state.num_failed_processes = std::cmp::max(state.num_failed_processes, new_value);
        });
    }
}

// the min runahead time is updated by workers, so needs to be locked
struct ControllerScheduling {
    // minimum allowed runahead when sending events between nodes
    min_runahead_config: Option<SimulationTime>,
    smallest_latency: SimulationTime,
    // minimum allowed runahead when sending events between nodes
    min_runahead: Option<SimulationTime>,
    // the simulator should attempt to end immediately after this time
    end_time: EmulatedTime,
}

impl ControllerScheduling {
    fn next_interval_window(
        &self,
        min_next_event_time: EmulatedTime,
    ) -> (EmulatedTime, EmulatedTime) {
        // If the min_runahead is None, we haven't yet been given a latency value to base our
        // runahead off of (or dynamic runahead is disabled). We use the smallest latency between
        // any in-use graph nodes to start.
        let runahead = self.min_runahead.unwrap_or(self.smallest_latency);

        // the 'runahead' config option sets a lower bound for the runahead
        let runahead_config = self.min_runahead_config.unwrap_or(SimulationTime::ZERO);
        let runahead = std::cmp::max(runahead, runahead_config);
        assert_ne!(runahead, SimulationTime::ZERO);

        let start = min_next_event_time;

        // update the new window end as one interval past the new window start, making sure we don't
        // run over the experiment end time
        let end = min_next_event_time
            .checked_add(runahead)
            .unwrap_or(EmulatedTime::MAX);
        let end = std::cmp::min(end, self.end_time);

        (start, end)
    }
}

#[derive(Debug)]
pub struct ShadowStatusBarState {
    start: std::time::Instant,
    current: EmulatedTime,
    end: EmulatedTime,
    num_failed_processes: u32,
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
            sim_current.fmt_hr_min_sec(),
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

enum StatusLogger<T: status_bar::StatusBarState> {
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

    pub fn stop(&mut self) {
        match self {
            Self::Printer(x) => x.stop(),
            Self::Bar(x) => x.stop(),
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

    #[no_mangle]
    pub extern "C" fn controller_incrementPluginErrors(controller: *const Controller) {
        let controller = unsafe { controller.as_ref() }.unwrap();
        controller.increment_plugin_errors();
    }
}
