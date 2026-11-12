use std::net::SocketAddr;
use std::str::FromStr;

use quiche::Config;

use crate::config::{self, ServerArgs};
use crate::connection::ConnectionManager;

// The official quiche server example is a good guide:
// https://github.com/cloudflare/quiche/blob/master/quiche/examples/server.rs

pub fn run(args: ServerArgs) -> anyhow::Result<()> {
    log::info!("Running quic perf in server mode");

    // Validate the address.
    let local = SocketAddr::from_str(args.common.bind.as_str())?;

    // Set up quic config.
    let mut config = config::init_quic_config()?;

    // The server needs a crypto cert and key.
    setup_crypto(&mut config)?;

    // Initiate quic connection in server mode.
    let mut mgr = smol::block_on(ConnectionManager::listen(local, config))?;

    // Transfer the configured amount of stream data.
    smol::block_on(mgr.into_transfer(args.common.num_bytes))?;

    log::info!(
        "Quic perf server successfully transferred {} bytes!",
        args.common.num_bytes
    );
    Ok(())
}

fn setup_crypto(config: &mut Config) -> anyhow::Result<()> {
    // Set up a dummy cert and keys.
    let pair = rcgen::generate_simple_self_signed(vec!["localhost".to_string()])?;
    let cert_pem = pair.cert.pem();
    let key_pem = pair.signing_key.serialize_pem();

    // Write them to temporary files.
    let cert_path = "tmp_server_cert.crt";
    let key_path = "tmp_server_key.key";
    std::fs::write(cert_path, cert_pem)?;
    std::fs::write(key_path, key_pem)?;

    // Load the credentials into our quic config.
    config.load_cert_chain_from_pem_file(cert_path)?;
    config.load_priv_key_from_pem_file(key_path)?;

    Ok(())
}
