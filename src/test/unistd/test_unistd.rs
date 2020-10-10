/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::process;


fn main() {
    test_getpid_nodeps()
}


// Tests that the results are plausible, but can't really validate that it's our
// pid without depending on other functionality.
fn test_getpid_nodeps() {
    let pid = process::id();
    assert!(pid > 0);
    assert_eq!(pid, process::id());
}

