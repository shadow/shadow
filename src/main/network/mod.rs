//! The network simulation.
//!
//! This contains code that simulates the Internet and upstream routers. It does not contain any
//! emulation of Linux networking behaviour, which exists in the [`crate::host`] module.

use std::net::Ipv4Addr;

use crate::network::packet::PacketRc;

pub mod dns;
pub mod graph;
pub mod packet;
pub mod relay;
pub mod router;

pub trait PacketDevice {
    fn get_address(&self) -> Ipv4Addr;
    fn pop(&self) -> Option<PacketRc>;
    fn push(&self, packet: PacketRc);
}

#[cfg(test)]
mod tests {
    use shadow_shim_helper_rs::{emulated_time::EmulatedTime, simulation_time::SimulationTime};

    pub fn mock_time_millis(millis_since_sim_start: u64) -> EmulatedTime {
        let simtime = SimulationTime::from_millis(millis_since_sim_start);
        EmulatedTime::from_abs_simtime(simtime)
    }
}
