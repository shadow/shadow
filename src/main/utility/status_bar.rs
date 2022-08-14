use std::io::Write;
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, RwLock};
use std::time::Duration;

const SAVE_CURSOR: &str = "\u{1B}[s";
const RESTORE_CURSOR: &str = "\u{1B}[u";
const NEXT_LINE: &str = "\u{1B}[1E";
const PREV_LINE: &str = "\u{1B}[1F";
const CLEAR: &str = "\u{1B}[K";
const RESTORE_SCROLL_REGION: &str = "\u{1B}[r";
const LAST_LINE: &str = "\u{1B}[9999H";

pub trait StatusBarState: std::fmt::Display + std::marker::Send + std::marker::Sync {}
impl<T> StatusBarState for T where T: std::fmt::Display + std::marker::Send + std::marker::Sync {}

pub struct StatusBar<T: StatusBarState> {
    state: Arc<RwLock<T>>,
    stop_flag: Arc<AtomicBool>,
    thread: Option<std::thread::JoinHandle<()>>,
}

impl<T: 'static + StatusBarState> StatusBar<T> {
    /// Create and start drawing the status bar.
    pub fn new(state: T, redraw_interval: Duration) -> Self {
        let state = Arc::new(RwLock::new(state));
        let stop_flag = Arc::new(AtomicBool::new(false));

        Self {
            state: Arc::clone(&state),
            stop_flag: Arc::clone(&stop_flag),
            thread: Some(std::thread::spawn(move || {
                Self::redraw_loop(state, stop_flag, redraw_interval);
            })),
        }
    }

    fn redraw_loop(state: Arc<RwLock<T>>, stop_flag: Arc<AtomicBool>, redraw_interval: Duration) {
        // we re-draw the status bar every interval, even if the state hasn't changed, since the
        // terminal might have been resized and the scroll region might have been reset
        while !stop_flag.load(std::sync::atomic::Ordering::Acquire) {
            // the window size might change during the simulation, so we re-check it each time
            let rows = match tiocgwinsz() {
                Ok(x) => x.ws_row,
                Err(e) => {
                    log::error!("Status bar ioctl failed ({}). Stopping the status bar.", e);
                    break;
                }
            };

            if rows > 1 {
                #[rustfmt::skip]
                let to_print = [
                    // Restore the scroll region since some terminals handle scroll regions
                    // differently. For example, when using '{next_line}' some terminals will
                    // allow the cursor to move outside of the scroll region, and others don't.
                    SAVE_CURSOR, RESTORE_SCROLL_REGION, RESTORE_CURSOR,
                    // This will scroll the buffer up only if the cursor is on the last row.
                    SAVE_CURSOR, "\n", RESTORE_CURSOR,
                    // This will move the cursor up only if the cursor is on the last row (to
                    // match the previous scroll behaviour).
                    NEXT_LINE, PREV_LINE,
                    // The cursor is currently at the correct location, so save it for later.
                    SAVE_CURSOR,
                    // Set the scroll region to include all rows but the last.
                    &format!("\u{1B}[1;{}r", rows - 1),
                    // Move to the last row and write the message.
                    LAST_LINE, &format!("{}", *state.read().unwrap()), CLEAR,
                    // Restore the cursor position.
                    RESTORE_CURSOR,
                ]
                .join("");

                // We want to write everything in as few write() syscalls as possible. Note that
                // if we were to use eprint! with a format string like "{}{}", eprint! would
                // always make at least two write() syscalls, which we wouldn't want.
                std::io::stderr().write_all(to_print.as_bytes()).unwrap();
                let _ = std::io::stderr().flush();
            }
            std::thread::sleep(redraw_interval);
        }

        let to_print = format!(
            "{save_cursor}{last_line}{clear}{restore_scroll_region}{restore_cursor}",
            save_cursor = SAVE_CURSOR,
            last_line = LAST_LINE,
            clear = CLEAR,
            restore_scroll_region = RESTORE_SCROLL_REGION,
            restore_cursor = RESTORE_CURSOR,
        );

        std::io::stderr().write_all(to_print.as_bytes()).unwrap();
        let _ = std::io::stderr().flush();
    }

    /// Stop and remove the status bar.
    pub fn stop(&mut self) {
        self.stop_flag
            .swap(true, std::sync::atomic::Ordering::Relaxed);
        if let Some(handle) = self.thread.take() {
            if let Err(e) = handle.join() {
                log::warn!("Progress bar thread did not exit cleanly: {:?}", e);
            }
        }
    }

    /// Update the state of the status bar.
    pub fn mutate_state(&self, f: impl FnOnce(&mut T)) {
        f(&mut *self.state.write().unwrap())
    }
}

pub struct StatusPrinter<T: StatusBarState> {
    state: Arc<RwLock<T>>,
    stop_sender: Option<crossbeam::channel::Sender<()>>,
    thread: Option<std::thread::JoinHandle<()>>,
}

impl<T: 'static + StatusBarState> StatusPrinter<T> {
    /// Create and start printing the status.
    pub fn new(state: T) -> Self {
        let state = Arc::new(RwLock::new(state));
        //let (stop_sender, stop_receiver) = std::sync::mpsc::channel();
        let (stop_sender, stop_receiver) = crossbeam::channel::bounded(1);

        Self {
            state: Arc::clone(&state),
            stop_sender: Some(stop_sender),
            thread: Some(std::thread::spawn(move || {
                Self::print_loop(state, stop_receiver);
            })),
        }
    }

    fn print_loop(state: Arc<RwLock<T>>, stop_receiver: crossbeam::channel::Receiver<()>) {
        let print_interval = Duration::from_secs(60);

        loop {
            match stop_receiver.recv_timeout(print_interval) {
                // the sender disconnects to signal that we should stop
                Err(crossbeam::channel::RecvTimeoutError::Disconnected) => break,
                Err(crossbeam::channel::RecvTimeoutError::Timeout) => {}
                Ok(()) => unreachable!(),
            }

            // We want to write everything in as few write() syscalls as possible. Note that
            // if we were to use eprint! with a format string like "{}{}", eprint! would
            // always make at least two write() syscalls, which we wouldn't want.
            let to_write = format!("Progress: {}\n", *state.read().unwrap());
            std::io::stderr().write_all(to_write.as_bytes()).unwrap();
            let _ = std::io::stderr().flush();
        }
    }

    /// Stop printing the status.
    pub fn stop(&mut self) {
        // drop the sender to disconnect it
        self.stop_sender.take();
        if let Some(handle) = self.thread.take() {
            if let Err(e) = handle.join() {
                log::warn!("Progress thread did not exit cleanly: {:?}", e);
            }
        }
    }

    /// Update the state of the status.
    pub fn mutate_state(&self, f: impl FnOnce(&mut T)) {
        f(&mut *self.state.write().unwrap())
    }
}

nix::ioctl_read_bad!(_tiocgwinsz, libc::TIOCGWINSZ, libc::winsize);

fn tiocgwinsz() -> nix::Result<libc::winsize> {
    let mut win_size: libc::winsize = unsafe { std::mem::zeroed() };
    unsafe { _tiocgwinsz(0, &mut win_size)? };
    Ok(win_size)
}
