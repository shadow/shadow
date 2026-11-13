use crate::config::{self, ClientArgs};
use crate::connection::ConnectionManager;

// The official quiche client example is a good guide:
// https://github.com/cloudflare/quiche/blob/master/quiche/examples/client.rs

pub fn run(args: ClientArgs) -> anyhow::Result<()> {
    log::info!("Running quic perf in client mode");

    // Validate the address strings.
    let local = args.common.bind;
    let peer = args.connect;

    // Set up quic config.
    let config = config::init_quic_config()?;

    // Initiate quic connection in client mode.
    let mgr = smol::block_on(ConnectionManager::connect(
        local,
        peer,
        config,
        args.common.sqlog_path,
    ))?;

    // Transfer the configured amount of stream data.
    smol::block_on(mgr.into_transfer(args.common.num_bytes))?;

    log::info!(
        "Quic perf client successfully transferred {} bytes!",
        args.common.num_bytes
    );
    Ok(())
}
