use std::net::Ipv4Addr;

use crate::network::packet::Packet;

pub mod graph;
pub mod net_namespace;
pub mod packet;
pub mod relay;
pub mod router;

pub trait PacketDevice {
    fn get_address(&self) -> Ipv4Addr;
    fn pop(&self) -> Option<Packet>;
    fn push(&self, packet: Packet);
}

#[cfg(test)]
mod tests {
    use shadow_shim_helper_rs::{emulated_time::EmulatedTime, simulation_time::SimulationTime};

    pub fn mock_time_millis(millis_since_sim_start: u64) -> EmulatedTime {
        let simtime = SimulationTime::from_millis(millis_since_sim_start);
        EmulatedTime::from_abs_simtime(simtime)
    }
}
