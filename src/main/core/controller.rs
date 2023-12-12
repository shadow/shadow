use std::io::IsTerminal;
use std::sync::Arc;
use std::time::Duration;

use anyhow::Context;
use rand::SeedableRng;
use rand_xoshiro::Xoshiro256PlusPlus;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::util::time::TimeParts;

use crate::core::configuration::ConfigOptions;
use crate::core::manager::{Manager, ManagerConfig};
use crate::core::preload::PreloadFiles;
use crate::core::sim_config::SimConfig;
use crate::core::worker;
use crate::utility::status_bar::{self, StatusBar, StatusPrinter};

pub struct Controller<'a> {
    // general options and user configuration for the simulation
    config: &'a ConfigOptions,
    sim_config: Option<SimConfig>,
    preload_files: &'a PreloadFiles,

    // the simulator should attempt to end immediately after this time
    end_time: EmulatedTime,
}

impl<'a> Controller<'a> {
    pub fn new(
        sim_config: SimConfig,
        config: &'a ConfigOptions,
        preload_files: &'a PreloadFiles,
    ) -> Self {
        let end_time: Duration = config.general.stop_time.unwrap().into();
        let end_time: SimulationTime = end_time.try_into().unwrap();
        let end_time = EmulatedTime::SIMULATION_START + end_time;

        Self {
            config,
            sim_config: Some(sim_config),
            preload_files,
            end_time,
        }
    }

    pub fn run(mut self) -> anyhow::Result<()> {
        let mut sim_config = self.sim_config.take().unwrap();

        let status_logger = self.config.general.progress.unwrap().then(|| {
            let state = ShadowStatusBarState::new(self.end_time);

            if std::io::stderr().lock().is_terminal() {
                let redraw_interval = Duration::from_millis(1000);
                StatusLogger::Bar(StatusBar::new(state, redraw_interval))
            } else {
                StatusLogger::Printer(StatusPrinter::new(state))
            }
        });

        let manager_config = ManagerConfig {
            random: Xoshiro256PlusPlus::from_rng(&mut sim_config.random).unwrap(),
            ip_assignment: sim_config.ip_assignment,
            routing_info: sim_config.routing_info,
            host_bandwidths: sim_config.host_bandwidths,
            hosts: sim_config.hosts,
        };

        let manager = Manager::new(
            manager_config,
            &self,
            self.config,
            self.preload_files,
            self.end_time,
        )
        .context("Failed to initialize the manager")?;

        log::info!("Running simulation");
        let num_plugin_errors = manager.run(status_logger.as_ref().map(|x| x.status()))?;
        log::info!("Finished simulation");

        if num_plugin_errors > 0 {
            return Err(anyhow::anyhow!(
                "{num_plugin_errors} managed processes in unexpected final state"
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
}

impl SimController for Controller<'_> {
    fn manager_finished_current_round(
        &self,
        min_next_event_time: EmulatedTime,
    ) -> Option<(EmulatedTime, EmulatedTime)> {
        // TODO: once we get multiple managers, we have to block them here until they have all
        // notified us that they are finished

        let runahead = worker::WORKER_SHARED
            .borrow()
            .as_ref()
            .unwrap()
            .runahead
            .get();
        assert_ne!(runahead, SimulationTime::ZERO);

        let new_start = min_next_event_time;

        // update the new window end as one interval past the new window start, making sure we don't
        // run over the experiment end time
        let new_end = new_start.checked_add(runahead).unwrap_or(EmulatedTime::MAX);
        let new_end = std::cmp::min(new_end, self.end_time);

        let continue_running = new_start < new_end;
        continue_running.then_some((new_start, new_end))
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

        let sim_current = TimeParts::from_nanos(sim_current.as_nanos());
        let sim_end = TimeParts::from_nanos(sim_end.as_nanos());
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
