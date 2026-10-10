use clap::{Args, Parser, Subcommand};
use quiche::{Config, Connection};

/// Runs a quic-based performance test between a quic client and a quic server.
#[derive(Parser)]
#[command(author, version, about, long_about = None)]
pub struct CliArgs {
    #[command(subcommand)]
    pub command: Command,
}

/// The perf test can be run in either client mode or server mode.
#[derive(Subcommand)]
pub enum Command {
    Client(ClientArgs),
    Server(ServerArgs),
}

#[derive(Args)]
pub struct ClientArgs {
    #[command(flatten)]
    pub common: CommonArgs,
    /// The address of the quic perf server to which we send messages.
    #[arg(short, long, value_name = "ADDR:PORT", required = true)]
    pub connect: String,
}

#[derive(Args)]
pub struct ServerArgs {
    #[command(flatten)]
    pub common: CommonArgs,
}

#[derive(Debug, Args)]
pub struct CommonArgs {
    /// The address to bind to when receiving messages (use port 0 to auto-select).
    #[arg(short, long, value_name = "ADDR:PORT", default_value = "0.0.0.0:0")]
    pub bind: String,
    /// Number of bytes to send to the quic peer.
    #[arg(short, long, value_name = "N", default_value = "1024")]
    pub num_bytes: usize,
}

pub fn parse_cli_args() -> CliArgs {
    CliArgs::parse()
}

/// Create a new quic config. Both the client and server should use this to
/// ensure protocol alignment.
pub fn init_quic_config() -> anyhow::Result<Config> {
    // Synchronize the client and server protocol versions.
    let mut config = Config::new(quiche::PROTOCOL_VERSION)?;
    config.set_application_protos(&[b"quic-perf-test-shadow"])?;

    // Bypass verification checks to allow self-signed certificates.
    config.verify_peer(false);

    // Set the congestion control algorithm.
    config.set_cc_algorithm(quiche::CongestionControlAlgorithm::CUBIC);

    // Allow stream data to be transferred.
    config.set_initial_max_streams_bidi(1);
    config.set_initial_max_streams_uni(1);
    config.set_initial_max_data(1_000_000);
    config.set_initial_max_stream_data_bidi_local(1_000_000);
    config.set_initial_max_stream_data_bidi_remote(1_000_000);
    config.set_initial_max_stream_data_uni(1_000_000);

    // No need to support session resumption.
    config.set_disable_active_migration(true);

    // After our transfer finishes, close the idle connection.
    config.set_max_idle_timeout(1_000);

    Ok(config)
}

/// Sets quic to store qlogs in the path specified in the env var `QLOGDIR` or
/// else in the local directory if `QLOGDIR` is not set.
pub fn enable_quic_qlog(conn: &mut Connection, prefix: &str) -> std::io::Result<()> {
    // Use the local dir if the env var is not set.
    let qlog_dir = std::env::var("QLOGDIR").unwrap_or(String::from("./"));

    // Use the sqlog suffix to indicate quiche's "streaming qlog" format.
    let fname = format!("{prefix}-trace.sqlog");

    // Create the file.
    let path = std::path::Path::new(&qlog_dir).join(&fname);
    let file = std::fs::File::create(path)?;

    // Configure the connection to store performance stats in the file.
    conn.set_qlog(
        Box::new(file),
        format!("{prefix}-trace"),
        format!("Quic {prefix} perf test"),
    );

    Ok(())
}
