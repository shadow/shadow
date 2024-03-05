use futures::prelude::*;
use libp2p::{multiaddr::Protocol, noise, ping, swarm::SwarmEvent, tcp, yamux, Multiaddr};
use std::{error::Error, time::Duration};

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Usage: ./test_libp2p_ping [listen-port] [peer-multiaddr]

    let mut swarm = libp2p::SwarmBuilder::with_new_identity()
        .with_tokio()
        .with_tcp(
            tcp::Config::default(),
            noise::Config::new,
            yamux::Config::default,
        )?
        .with_behaviour(|_| ping::Behaviour::default())?
        .with_swarm_config(|cfg| cfg.with_idle_connection_timeout(Duration::from_secs(u64::MAX)))
        .build();

    // Set the listening port to the one given as the first command-line
    // argument or zero if it is not given.
    let mut addr: Multiaddr = "/ip4/0.0.0.0".parse()?;
    let port = match std::env::args().nth(1) {
        Some(port) => port.parse()?,
        None => 0,
    };
    addr.push(Protocol::Tcp(port));

    // Listen for the incoming peers.
    swarm.listen_on(addr)?;

    // Dial the peer identified by the multi-address given as the second
    // command-line argument, if any.
    if let Some(addr) = std::env::args().nth(2) {
        let remote: Multiaddr = addr.parse()?;
        swarm.dial(remote)?;
        println!("Dialed {addr}")
    }

    loop {
        match swarm.select_next_some().await {
            SwarmEvent::NewListenAddr { address, .. } => println!("Listening on {address:?}"),
            SwarmEvent::Behaviour(event) => println!("{event:?}"),
            _ => {}
        }
    }
}
