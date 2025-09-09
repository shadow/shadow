use std::sync::RwLock;

use shadow_shim_helper_rs::simulation_time::SimulationTime;

/// Decides on the runahead for the next simulation round (the duration of the round).
///
/// Having a larger runahead improves performance since more hosts and more events can be run in
/// parallel during a simulation round, but if the runahead is too large then packets will be
/// delayed until the next simulation round which is beyond their intended latency. This uses a
/// fixed runahead of the provided minimum possible latency when dynamic runahead is disabled, and
/// otherwise uses a dynamic runahead of the minimum used latency. Both runahead calculations have a
/// static lower bound.
#[derive(Debug)]
pub struct Runahead {
    /// The lowest packet latency that shadow has used so far in the simulation. For performance, is
    /// only updated if dynamic runahead is enabled for the simulation.
    min_used_latency: RwLock<Option<SimulationTime>>,
    /// The lowest latency that's possible in the simulation (the graph edge with the lowest
    /// latency).
    min_possible_latency: SimulationTime,
    /// A lower bound for the runahead as specified by the user.
    min_runahead_config: Option<SimulationTime>,
    /// Is dynamic runahead enabled?
    is_runahead_dynamic: bool,
}

impl Runahead {
    pub fn new(
        is_runahead_dynamic: bool,
        min_possible_latency: SimulationTime,
        min_runahead_config: Option<SimulationTime>,
    ) -> Self {
        assert!(!min_possible_latency.is_zero());

        Self {
            min_used_latency: RwLock::new(None),
            min_possible_latency,
            min_runahead_config,
            is_runahead_dynamic,
        }
    }

    /// Get the runahead for the next round.
    pub fn get(&self) -> SimulationTime {
        // If the 'min_used_latency' is None, we haven't yet been given a latency value to base our
        // runahead off of (or dynamic runahead is disabled). We use the smallest possible latency
        // to start.
        let runahead = self
            .min_used_latency
            .read()
            .unwrap()
            .unwrap_or(self.min_possible_latency);

        // the 'runahead' config option sets a lower bound for the runahead
        let runahead_config = self.min_runahead_config.unwrap_or(SimulationTime::ZERO);
        std::cmp::max(runahead, runahead_config)
    }

    /// If dynamic runahead is enabled, will compare and update the stored lowest packet latency.
    /// This may shorten the runahead for future rounds.
    pub fn update_lowest_used_latency(&self, latency: SimulationTime) {
        assert!(latency > SimulationTime::ZERO);

        // if dynamic runahead is disabled, we don't update 'min_used_latency'
        if !self.is_runahead_dynamic {
            return;
        }

        // helper function for checking if we should update the min_used_latency
        let should_update = |min_used_latency: &Option<SimulationTime>| {
            if let Some(min_used_latency) = min_used_latency
                && latency >= *min_used_latency
            {
                return false;
            }

            // true if runahead was never set before, or new latency is smaller than the old latency
            true
        };

        // an initial check with only a read lock
        {
            let min_used_latency = self.min_used_latency.read().unwrap();

            if !should_update(&min_used_latency) {
                return;
            }
        }

        let old_runahead;
        let min_runahead_config;

        // check the same condition again, but with a write lock
        {
            let mut min_used_latency = self.min_used_latency.write().unwrap();

            if !should_update(&min_used_latency) {
                return;
            }

            // cache the values for logging
            old_runahead = *min_used_latency;
            min_runahead_config = self.min_runahead_config;

            // update the min runahead
            *min_used_latency = Some(latency);
        }

        // these info messages may appear out-of-order in the log
        log::info!(
            "Minimum time runahead for next scheduling round updated from {:?} \
             to {} ns; the minimum config override is {:?} ns",
            old_runahead.map(|x| x.as_nanos()),
            latency.as_nanos(),
            min_runahead_config.map(|x| x.as_nanos())
        );
    }
}
