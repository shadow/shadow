/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::error::Error;

// Should be > the stoptime from the test config for this test to be meaningful
const NUM_SECONDS: u64 = 3;

fn main() -> Result<(), Box<dyn Error>> {
    // Test the case where multiple threads are running when simulation ends.
    let child = std::thread::spawn(|| {
        std::thread::sleep(std::time::Duration::from_secs(NUM_SECONDS));
    });
    std::thread::sleep(std::time::Duration::from_secs(NUM_SECONDS));

    // The shadow tests should never get here, but including it for completion.
    child.join().unwrap();
    println!("Threads exiting normally after {NUM_SECONDS} seconds");
    Ok(())
}
