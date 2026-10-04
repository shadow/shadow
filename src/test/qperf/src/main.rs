use env_logger::{Builder, Env, Target};

mod client;
pub mod config;
pub mod connection;
mod server;
pub mod stream;

fn main() -> anyhow::Result<()> {
    let args = config::parse_cli_args();
    setup_logging();
    match args.command {
        config::Command::Client(cargs) => client::run(cargs),
        config::Command::Server(sargs) => server::run(sargs),
    }
}

fn setup_logging() {
    Builder::from_env(Env::default().default_filter_or("info"))
        .format_timestamp_micros()
        .target(Target::Stdout)
        .init();
}
