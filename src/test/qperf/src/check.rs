use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Duration;

use anyhow::{Context, Result, bail};
use clap::Parser;
use serde_json::{Map, Value};

#[derive(Parser, Debug)]
#[command(about = "Validate QUIC performance from an sqlog")]
struct Args {
    /// Input .sqlog file
    sqlog: PathBuf,

    /// Expected minimum RTT in milliseconds
    #[arg(long)]
    rtt_min_ms: Option<f64>,

    /// Allowed minimum RTT deviation in milliseconds
    #[arg(long, default_value_t = 2.0)]
    rtt_min_ms_tol: f64,

    /// Expected median RTT in milliseconds
    #[arg(long)]
    rtt_med_ms: Option<f64>,

    /// Allowed median RTT deviation in milliseconds
    #[arg(long, default_value_t = 10.0)]
    rtt_med_ms_tol: f64,

    /// Expected loss percentage
    #[arg(long)]
    loss_percent: Option<f64>,

    /// Allowed loss deviation in percentage points
    #[arg(long, default_value_t = 1.0)]
    loss_percent_tol: f64,

    /// Expected upstream (outgoing) bandwidth in Mbit/s
    #[arg(long)]
    bw_up_mbps: Option<f64>,

    /// Expected downstream (incoming) bandwidth in Mbit/s
    #[arg(long)]
    bw_down_mbps: Option<f64>,

    /// Allowed bandwidth deviation (floor) in percent
    #[arg(long, default_value_t = 20.0)]
    bw_percent_tol: f64,

    /// Minimum application bytes needed to calculate goodput
    #[arg(long, default_value_t = 0)]
    min_goodput_bytes: u64,

    /// In flight bytes allowed to exceed cwnd by this percent
    #[arg(long, default_value_t = 10.0)]
    cwnd_flight_tol: f64,
}

fn main() -> ExitCode {
    env_logger::init();
    let args = Args::parse();
    match run(args) {
        Ok(()) => {
            log::info!("PASS ✅");
            ExitCode::SUCCESS
        }
        Err(e) => {
            log::error!("FAIL ❌: {e:?}");
            ExitCode::FAILURE
        }
    }
}

fn run(args: Args) -> Result<()> {
    log::info!("Running the qperf check utility with args: {args:?}");

    log::info!("Parsing sqlog file at {:?}", args.sqlog);

    // First parse the log file and extract the metrics.
    let records = QLogRecords::try_from(args.sqlog)?;

    let mut metrics = Metrics::default();
    for record in records.into_iter() {
        metrics.append(&mut Metrics::try_from(record)?);
    }

    log::info!("Found {} total metrics", metrics.len());

    // Now compute some high-level stats.
    let events = metrics.len();
    let packets_sent = metrics.packets_sent();
    let packets_received = metrics.packets_received();
    let packets_lost = metrics.packets_lost();
    let packets_retransmitted = metrics.packets_retransmitted();
    let network_bytes_sent = metrics.network_bytes_sent();
    let network_bytes_received = metrics.network_bytes_received();
    let stream_bytes_sent = metrics.stream_bytes_sent();
    let stream_bytes_received = metrics.stream_bytes_received();

    let rtt_ms_min = metrics.rtt_quantile(0.0);
    let rtt_ms_p25 = metrics.rtt_quantile(0.25);
    let rtt_ms_p50 = metrics.rtt_quantile(0.5);
    let rtt_ms_p75 = metrics.rtt_quantile(0.75);
    let rtt_ms_p95 = metrics.rtt_quantile(0.95);
    let rtt_ms_max = metrics.rtt_quantile(1.0);

    let rtt_variance_ms_min = metrics.rtt_variance_quantile(0.0);
    let rtt_variance_ms_p25 = metrics.rtt_variance_quantile(0.25);
    let rtt_variance_ms_p50 = metrics.rtt_variance_quantile(0.5);
    let rtt_variance_ms_p75 = metrics.rtt_variance_quantile(0.75);
    let rtt_variance_ms_p95 = metrics.rtt_variance_quantile(0.95);
    let rtt_variance_ms_max = metrics.rtt_variance_quantile(1.0);

    let rtt_smoothed_ms_min = metrics.rtt_smoothed_quantile(0.0);
    let rtt_smoothed_ms_p25 = metrics.rtt_smoothed_quantile(0.25);
    let rtt_smoothed_ms_p50 = metrics.rtt_smoothed_quantile(0.5);
    let rtt_smoothed_ms_p75 = metrics.rtt_smoothed_quantile(0.75);
    let rtt_smoothed_ms_p95 = metrics.rtt_smoothed_quantile(0.95);
    let rtt_smoothed_ms_max = metrics.rtt_smoothed_quantile(1.0);

    let pto_max = metrics.pto_max();
    let congestion_window_max = metrics.congestion_window_max();
    let bytes_in_flight_max = metrics.bytes_in_flight_max();
    let packets_in_flight_max = metrics.packets_in_flight_max();

    let ss_threshold_min = metrics.ss_threshold_quantile(0.0);
    let ss_threshold_p50 = metrics.ss_threshold_quantile(0.5);
    let ss_threshold_max = metrics.ss_threshold_quantile(1.0);

    let loss_percent = metrics.loss_percent();
    let transfer_duration_ms = metrics.transfer_duration_ms();
    let goodput_mbps_up = metrics.goodput_mbps_up();
    let goodput_mbps_down = metrics.goodput_mbps_down();
    let throughput_mbps_up = metrics.throughput_mbps_up();
    let throughput_mbps_down = metrics.throughput_mbps_down();

    // Check that some stats are within the configured tolerances.
    let mut failures = Vec::<String>::new();

    // Minimum rount-trip time should be within a tolerance of the expected minimum RTT.
    if let Some(expected) = args.rtt_min_ms
        && let Some(actual) = rtt_ms_min
        && (actual - expected).abs() > args.rtt_min_ms_tol
    {
        let msg = format!(
            "Minimum RTT {actual:.2} is outside {expected:.2} +/- {:.2}",
            args.rtt_min_ms_tol
        );
        failures.push(msg);
    }

    // Median rount-trip time should be within a tolerance of the expected median RTT.
    if let Some(expected) = args.rtt_med_ms
        && let Some(actual) = rtt_ms_p50
        && (actual - expected).abs() > args.rtt_med_ms_tol
    {
        let msg = format!(
            "Median RTT {actual:.2} is outside {expected:.2} +/- {:.2}",
            args.rtt_med_ms_tol
        );
        failures.push(msg);
    }

    // Packet loss should be within a tolerance of the expected loss.
    if let Some(expected) = args.loss_percent
        && let Some(actual) = loss_percent
        && (actual - expected).abs() > args.loss_percent_tol
    {
        let msg = format!(
            "loss {actual:.2}% is outside {expected:.2}% +/- {:.2}%",
            args.loss_percent_tol
        );
        failures.push(msg);
    }

    // Transfer of packets and application bytes.
    if packets_sent == 0 {
        failures.push("no packet_sent events found".to_string());
    }
    if packets_received == 0 {
        failures.push("no packets_received events found".to_string());
    }
    if stream_bytes_sent == 0 {
        failures.push("no stream_bytes_sent events found".to_string());
    }
    if stream_bytes_received == 0 {
        failures.push("no stream_bytes_received events found".to_string());
    }

    // Goodput should be within a tolerance of expected bandwidth.
    if stream_bytes_sent > args.min_goodput_bytes
        && let Some(expected) = args.bw_up_mbps
        && let Some(actual) = goodput_mbps_up
    {
        check_goodput(expected, actual, args.bw_percent_tol, "up", &mut failures);
    }
    if stream_bytes_received > args.min_goodput_bytes
        && let Some(expected) = args.bw_down_mbps
        && let Some(actual) = goodput_mbps_down
    {
        check_goodput(expected, actual, args.bw_percent_tol, "down", &mut failures);
    }

    // In flight bytes should not exceed congestion window.
    if let Some(cwnd_max) = congestion_window_max
        && let Some(flight_max) = bytes_in_flight_max
    {
        let tol = args.cwnd_flight_tol.clamp(0.0, 100.0);
        let threshold = (cwnd_max as f64) * (1.0 + (tol / 100.0));

        if flight_max as f64 > threshold {
            let msg = format!(
                "bytes_in_flight {flight_max} exceeds congestion_window {cwnd_max} \
                by more than {:.2}%",
                args.cwnd_flight_tol
            );
            failures.push(msg);
        }
    }

    // Write out the results.
    let output = serde_json::json!({
        "events": events,
        "packets_sent": packets_sent,
        "packets_received": packets_received,
        "packets_lost": packets_lost,
        "packets_retransmitted": packets_retransmitted,
        "network_bytes_sent": network_bytes_sent,
        "network_bytes_received": network_bytes_received,
        "stream_bytes_sent": stream_bytes_sent,
        "stream_bytes_received": stream_bytes_received,
        "rtt_ms_min": rtt_ms_min,
        "rtt_ms_p25": rtt_ms_p25,
        "rtt_ms_p50": rtt_ms_p50,
        "rtt_ms_p75": rtt_ms_p75,
        "rtt_ms_p95": rtt_ms_p95,
        "rtt_ms_max": rtt_ms_max,
        "rtt_variance_ms_min": rtt_variance_ms_min,
        "rtt_variance_ms_p25": rtt_variance_ms_p25,
        "rtt_variance_ms_p50": rtt_variance_ms_p50,
        "rtt_variance_ms_p75": rtt_variance_ms_p75,
        "rtt_variance_ms_p95": rtt_variance_ms_p95,
        "rtt_variance_ms_max": rtt_variance_ms_max,
        "rtt_smoothed_ms_min": rtt_smoothed_ms_min,
        "rtt_smoothed_ms_p25": rtt_smoothed_ms_p25,
        "rtt_smoothed_ms_p50": rtt_smoothed_ms_p50,
        "rtt_smoothed_ms_p75": rtt_smoothed_ms_p75,
        "rtt_smoothed_ms_p95": rtt_smoothed_ms_p95,
        "rtt_smoothed_ms_max": rtt_smoothed_ms_max,
        "pto_max": pto_max,
        "congestion_window_max": congestion_window_max,
        "bytes_in_flight_max": bytes_in_flight_max,
        "packets_in_flight_max": packets_in_flight_max,
        "ss_threshold_min": ss_threshold_min,
        "ss_threshold_p50": ss_threshold_p50,
        "ss_threshold_max": ss_threshold_max,
        "loss_percent": loss_percent,
        "transfer_duration_ms": transfer_duration_ms,
        "goodput_mbps_up": goodput_mbps_up,
        "goodput_mbps_down": goodput_mbps_down,
        "throughput_mbps_up": throughput_mbps_up,
        "throughput_mbps_down": throughput_mbps_down,
        "failures": failures,
    });

    eprintln!("{}", serde_json::to_string_pretty(&output)?);

    log::info!("Done! Summary metrics were written to stderr.");

    if failures.is_empty() {
        Ok(())
    } else {
        bail!(
            "{} metrics checks failed (see stderr for details)",
            failures.len()
        )
    }
}

fn check_goodput(
    expected: f64,
    actual: f64,
    percent_tol: f64,
    direction: &str,
    failures: &mut Vec<String>,
) {
    if actual > expected {
        let msg = format!(
            "goodput {direction} {actual:.2} Mbit/s exceeded \
            the theoretical maximum {expected:.2} Mbit/s"
        );
        failures.push(msg);
    }

    let tol = percent_tol.clamp(0.0, 100.0);
    let threshold = (1.0 - (tol / 100.0)) * expected;

    if actual < threshold {
        let msg = format!(
            "goodput {direction} {actual:.2} Mbit/s was less than \
            the allowed minimum threshold {threshold:.2} Mbit/s"
        );
        failures.push(msg);
    }
}

#[derive(Debug)]
struct QLogRecords {
    records: Vec<Record>,
}

impl TryFrom<PathBuf> for QLogRecords {
    type Error = anyhow::Error;

    fn try_from(path: PathBuf) -> Result<Self> {
        // Get the file contents.
        let contents = fs::read(&path).context(format!("failed to read {}", path.display()))?;

        // Store the records parsed from the file.
        let mut records = Vec::new();
        let mut n_rows = 0usize;

        // The file is in the JSON text sequence format (json-seq). Each record is
        // prefixed by an ASCII Record Separator (0x1E) and ends with an ASCII Line
        // Feed character (0x0A): https://datatracker.ietf.org/doc/rfc7464/
        //
        // Skip line 1: it's empty as an artifact of this split method.
        for record in contents.split(|&b| b == 0x1E).skip(1) {
            n_rows += 1;

            // Trim whitespace and the line feed.
            let trimmed = record.trim_ascii();

            if trimmed.is_empty() {
                log::warn!("Skipping empty record on row {n_rows}");
                continue;
            }

            // Get the serde value, and then convert to our typed record.
            match serde_json::from_slice::<Value>(trimmed) {
                Ok(value) => match Record::try_from(value) {
                    Ok(event) => records.push(event),
                    Err(e) => log::warn!("Skipping invalid JSON event on row {n_rows}: {e}"),
                },
                Err(e) => log::warn!("Skipping invalid JSON valueon row {n_rows}: {e}"),
            }
        }

        log::info!(
            "Found {} valid records and skipped {} invalid records",
            records.len(),
            n_rows - records.len()
        );

        Ok(Self { records })
    }
}

impl QLogRecords {
    pub fn into_iter(self) -> std::vec::IntoIter<Record> {
        self.records.into_iter()
    }
}

#[derive(Debug)]
enum Record {
    // We do not yet process the header.
    #[allow(dead_code)]
    Header(Header),
    Event(Event),
}

impl TryFrom<Value> for Record {
    type Error = anyhow::Error;

    fn try_from(value: Value) -> Result<Self> {
        let object = value
            .as_object()
            .context("failed to get object from JSON value")?;

        let record = if object.contains_key("file_schema") {
            Record::Header(Header::try_from(object.clone())?)
        } else {
            Record::Event(Event::try_from_ref(object)?)
        };

        Ok(record)
    }
}

#[derive(Debug)]
struct Header {
    _data: Map<String, Value>,
}

impl TryFrom<Map<String, Value>> for Header {
    type Error = anyhow::Error;

    fn try_from(object: Map<String, Value>) -> Result<Self> {
        // Event format: {"file_schema": ..., ...}
        Ok(Self { _data: object })
    }
}

#[derive(Debug)]
struct Event {
    time: f64,
    name: String,
    data: Map<String, Value>,
}

impl Event {
    // Like TryFrom, but with an object ref so we can avoid a clone.
    fn try_from_ref(object: &Map<String, Value>) -> Result<Self> {
        // Event format: {"time": ..., "name": ..., "data": {...}}
        let time = object
            .get("time")
            .context("missing the 'time' key in JSON object")?
            .as_f64()
            .context("incorrect type for 'time' value")?;

        let name = object
            .get("name")
            .context("missing the 'name' key in JSON object")?
            .as_str()
            .context("incorrect type for 'name' value")?
            .to_string();

        let data = object
            .get("data")
            .context("missing the 'data' key in JSON object")?
            .as_object()
            .context("incorrect type for 'data' value")?
            .clone();

        Ok(Event { time, name, data })
    }

    fn time(&self) -> Duration {
        // sqlog records start at t=0;
        Duration::from_secs_f64(self.time / 1_000.0)
    }

    // Get the raw packet length of the packet.
    fn get_raw_length(&self) -> Option<u64> {
        self.data
            .get("raw")
            .and_then(|raw| raw.get("length").and_then(Value::as_u64))
    }

    // Get the total payload length across all frames in the packet.
    fn get_frames_raw_payload_length(&self) -> Option<u64> {
        let frames = self.data.get("frames")?.as_array()?;

        let total_payload_len = frames
            .iter()
            .filter_map(|frame| {
                frame
                    .get("raw")
                    .and_then(|raw| raw.get("payload_length").and_then(Value::as_u64))
            })
            .sum();

        Some(total_payload_len)
    }

    fn get_f64(&self, key: &str) -> Option<f64> {
        self.data.get(key).and_then(Value::as_f64)
    }

    fn get_u64(&self, key: &str) -> Option<u64> {
        self.data.get(key).and_then(Value::as_u64)
    }

    fn get_string(&self, key: &str) -> Option<String> {
        self.data.get(key).and_then(Value::as_str).map(String::from)
    }
}

#[derive(Debug)]
enum Metric {
    PacketSent,
    PacketReceived,
    PacketLost,
    PacketRetransmitted,
    NetworkBytesSent(u64, Duration),
    NetworkBytesReceived(u64, Duration),
    StreamBytesSent(u64, Duration),
    StreamBytesReceived(u64, Duration),
    // Keeping in case we want to check in the future.
    #[allow(dead_code)]
    RttMin(f64),
    RttLatest(f64),
    RttSmoothed(f64),
    RttVariance(f64),
    CongestionWindow(u64),
    // TODO: might be worth at least logging these.
    #[allow(dead_code)]
    CongestionState(String),
    SsThresh(u64),
    BytesInFlight(u64),
    PacketsInFlight(u64),
    ProbeTimeout(u64),
    // We store these but do not process them yet.
    // These can be scanned for more possible metrics.
    #[allow(dead_code)]
    Unidentified(String),
}

#[derive(Debug, Default)]
struct Metrics {
    metrics: Vec<Metric>,
}

impl Metrics {
    fn append(&mut self, other: &mut Self) {
        self.metrics.append(&mut other.metrics);
    }

    fn len(&self) -> usize {
        self.metrics.len()
    }

    fn sum_by<T, F>(&self, extract: F) -> T
    where
        T: std::iter::Sum,
        F: Fn(&Metric) -> Option<T>,
    {
        self.metrics.iter().filter_map(extract).sum()
    }

    fn min_by<T, F>(&self, extract: F) -> Option<T>
    where
        T: Ord,
        F: Fn(&Metric) -> Option<T>,
    {
        self.metrics.iter().filter_map(extract).min()
    }

    fn max_by<T, F>(&self, extract: F) -> Option<T>
    where
        T: Ord,
        F: Fn(&Metric) -> Option<T>,
    {
        self.metrics.iter().filter_map(extract).max()
    }

    fn _min_f64_by<F>(&self, extract: F) -> Option<f64>
    where
        F: Fn(&Metric) -> Option<f64>,
    {
        self.metrics
            .iter()
            .filter_map(extract)
            .min_by(f64::total_cmp)
    }

    fn _max_f64_by<F>(&self, extract: F) -> Option<f64>
    where
        F: Fn(&Metric) -> Option<f64>,
    {
        self.metrics
            .iter()
            .filter_map(extract)
            .max_by(f64::total_cmp)
    }

    /// Computes the given quantile 'q' (e.g., 0.95 for 95th percentile).
    fn quantile_by<T, F>(&self, q: f64, extract: F) -> Option<T>
    where
        T: Clone + PartialOrd,
        F: Fn(&Metric) -> Option<T>,
    {
        // Collect all matching metric values.
        let mut values: Vec<T> = self.metrics.iter().filter_map(extract).collect();

        if values.is_empty() {
            return None;
        }

        // Sort the values in ascending order. Support floating-point values
        // with `partial_cmp`.
        values.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

        // Calculate the target index using the Nearest Rank method.
        let max_index = values.len() - 1;
        let q_index = q * (max_index as f64);
        let index = (q_index.round() as usize).clamp(0, max_index);

        // Return the value at that rank.
        Some(values[index].clone())
    }

    fn packets_sent(&self) -> usize {
        self.sum_by(|m| match m {
            Metric::PacketSent => Some(1),
            _ => None,
        })
    }

    fn packets_received(&self) -> usize {
        self.sum_by(|m| match m {
            Metric::PacketReceived => Some(1),
            _ => None,
        })
    }

    fn packets_lost(&self) -> usize {
        self.sum_by(|m| match m {
            Metric::PacketLost => Some(1),
            _ => None,
        })
    }

    fn packets_retransmitted(&self) -> usize {
        self.sum_by(|m| match m {
            Metric::PacketRetransmitted => Some(1),
            _ => None,
        })
    }

    fn network_bytes_sent(&self) -> u64 {
        self.sum_by(|m| match m {
            Metric::NetworkBytesSent(b, _) => Some(*b),
            _ => None,
        })
    }

    fn network_bytes_received(&self) -> u64 {
        self.sum_by(|m| match m {
            Metric::NetworkBytesReceived(b, _) => Some(*b),
            _ => None,
        })
    }

    fn stream_bytes_sent(&self) -> u64 {
        self.sum_by(|m| match m {
            Metric::StreamBytesSent(b, _) => Some(*b),
            _ => None,
        })
    }

    fn stream_bytes_received(&self) -> u64 {
        self.sum_by(|m| match m {
            Metric::StreamBytesReceived(b, _) => Some(*b),
            _ => None,
        })
    }

    fn rtt_quantile(&self, q: f64) -> Option<f64> {
        self.quantile_by(q, |m| match m {
            Metric::RttLatest(t) => Some(*t),
            _ => None,
        })
    }

    fn rtt_smoothed_quantile(&self, q: f64) -> Option<f64> {
        self.quantile_by(q, |m| match m {
            Metric::RttSmoothed(t) => Some(*t),
            _ => None,
        })
    }

    fn rtt_variance_quantile(&self, q: f64) -> Option<f64> {
        self.quantile_by(q, |m| match m {
            Metric::RttVariance(t) => Some(*t),
            _ => None,
        })
    }

    fn ss_threshold_quantile(&self, q: f64) -> Option<u64> {
        self.quantile_by(q, |m| match m {
            Metric::SsThresh(t) => Some(*t),
            _ => None,
        })
    }

    fn pto_max(&self) -> Option<u64> {
        self.max_by(|m| match m {
            Metric::ProbeTimeout(t) => Some(*t),
            _ => None,
        })
    }

    fn congestion_window_max(&self) -> Option<u64> {
        self.max_by(|m| match m {
            Metric::CongestionWindow(b) => Some(*b),
            _ => None,
        })
    }

    fn bytes_in_flight_max(&self) -> Option<u64> {
        self.max_by(|m| match m {
            Metric::BytesInFlight(b) => Some(*b),
            _ => None,
        })
    }

    fn packets_in_flight_max(&self) -> Option<u64> {
        self.max_by(|m| match m {
            Metric::PacketsInFlight(n) => Some(*n),
            _ => None,
        })
    }

    fn loss_percent(&self) -> Option<f64> {
        let sent = self.packets_sent();
        if sent > 0 {
            let loss = self.packets_lost();
            Some((loss as f64) * 100.0 / (sent as f64))
        } else {
            None
        }
    }

    fn transfer_duration_ms(&self) -> f64 {
        let filter = |m: &Metric| match m {
            Metric::StreamBytesSent(_, t) => Some(t.clone()),
            Metric::StreamBytesReceived(_, t) => Some(t.clone()),
            _ => None,
        };

        let first = self.min_by(filter);
        let last = self.max_by(filter);

        if let Some(f) = first
            && let Some(l) = last
        {
            (l - f).as_secs_f64() * 1_000.0
        } else {
            0.0
        }
    }

    fn mbit_per_sec_by<F>(&self, extract: F) -> Option<f64>
    where
        F: Fn(&Metric) -> Option<(u64, Duration)>,
    {
        let mut first: Option<Duration> = None;
        let mut last: Option<Duration> = None;
        let mut bytes: usize = 0;

        for metric in self.metrics.iter() {
            if let Some((b, t)) = extract(metric) {
                // Keep a sum of the bytes.
                bytes += b as usize;

                // Keep track of the min time.
                first = Some(first.map_or(t, |f| f.min(t)));

                // Keep track of the last time.
                last = Some(last.map_or(t, |f| f.max(t)));
            }
        }

        let (Some(first), Some(last)) = (first, last) else {
            return None;
        };

        let duration_secs = (last - first).as_secs_f64();
        if duration_secs == 0.0 {
            return None;
        }

        let mbit = (bytes * 8) as f64 / 1_000_000.0;

        Some(mbit / duration_secs)
    }

    fn goodput_mbps_up(&self) -> Option<f64> {
        self.mbit_per_sec_by(|m| match m {
            Metric::StreamBytesSent(b, t) => Some((*b, *t)),
            _ => None,
        })
    }

    fn goodput_mbps_down(&self) -> Option<f64> {
        self.mbit_per_sec_by(|m| match m {
            Metric::StreamBytesReceived(b, t) => Some((*b, *t)),
            _ => None,
        })
    }

    fn throughput_mbps_up(&self) -> Option<f64> {
        self.mbit_per_sec_by(|m| match m {
            Metric::NetworkBytesSent(b, t) => Some((*b, *t)),
            _ => None,
        })
    }

    fn throughput_mbps_down(&self) -> Option<f64> {
        self.mbit_per_sec_by(|m| match m {
            Metric::NetworkBytesReceived(b, t) => Some((*b, *t)),
            _ => None,
        })
    }
}

impl TryFrom<Record> for Metrics {
    type Error = anyhow::Error;

    fn try_from(value: Record) -> Result<Self> {
        match value {
            Record::Header(_) => Ok(Metrics::default()), // Header has no metrics.
            Record::Event(event) => Metrics::try_from(event),
        }
    }
}

impl TryFrom<Event> for Metrics {
    type Error = anyhow::Error;

    fn try_from(event: Event) -> Result<Self> {
        let mut metrics = Vec::new();

        // Example data layout for quic:packet_* event types.
        // "data":{
        //    "header":{"packet_type":"1RTT","packet_number":13},
        //    "raw":{"length":1200,"payload_length":1166},
        //    "send_at_time":200.0,
        //    "frames":[
        //       {"frame_type":"stream","stream_id":0,"offset":9114,"raw":{"payload_length":1160}}
        //    ]
        // }

        match event.name.as_str() {
            "quic:packet_sent" => {
                metrics.push(Metric::PacketSent);
                if let Some(len) = event.get_raw_length() {
                    metrics.push(Metric::NetworkBytesSent(len, event.time()))
                }
                if let Some(len) = event.get_frames_raw_payload_length() {
                    metrics.push(Metric::StreamBytesSent(len, event.time()))
                }
            }

            "quic:packet_received" => {
                metrics.push(Metric::PacketReceived);
                if let Some(len) = event.get_raw_length() {
                    metrics.push(Metric::NetworkBytesReceived(len, event.time()))
                }
                if let Some(len) = event.get_frames_raw_payload_length() {
                    metrics.push(Metric::StreamBytesReceived(len, event.time()))
                }
            }

            "quic:packet_lost" => {
                metrics.push(Metric::PacketLost);
            }

            "quic:packet_retransmitted" => {
                metrics.push(Metric::PacketRetransmitted);
            }

            "quic:recovery_metrics_updated" => {
                if let Some(val) = event.get_f64("min_rtt") {
                    metrics.push(Metric::RttMin(val));
                }
                if let Some(val) = event.get_f64("min_rtt") {
                    metrics.push(Metric::RttLatest(val));
                }
                if let Some(val) = event.get_f64("smoothed_rtt") {
                    metrics.push(Metric::RttSmoothed(val));
                }
                if let Some(val) = event.get_f64("rtt_variance") {
                    metrics.push(Metric::RttVariance(val));
                }
                if let Some(val) = event.get_u64("congestion_window") {
                    metrics.push(Metric::CongestionWindow(val));
                }
                if let Some(val) = event.get_u64("ssthresh") {
                    metrics.push(Metric::SsThresh(val));
                }
                if let Some(val) = event.get_u64("bytes_in_flight") {
                    metrics.push(Metric::BytesInFlight(val));
                }
                if let Some(val) = event.get_u64("packets_in_flight") {
                    metrics.push(Metric::PacketsInFlight(val));
                }
                if let Some(val) = event.get_u64("pto_count") {
                    metrics.push(Metric::ProbeTimeout(val));
                }
            }

            "quic:congestion_state_updated" => {
                if let Some(val) = event.get_string("new") {
                    metrics.push(Metric::CongestionState(val));
                }
            }

            _ => metrics.push(Metric::Unidentified(event.name.clone())),
        }

        Ok(Self { metrics })
    }
}
