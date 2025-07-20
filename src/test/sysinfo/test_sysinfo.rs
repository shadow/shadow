/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

fn main() {
    let mut info = unsafe { std::mem::zeroed::<libc::sysinfo>() };
    let info_ptr = &mut info as *mut libc::sysinfo;
    let rv = unsafe { libc::sysinfo(info_ptr) };

    println!("Found return value {rv:?}.");
    assert_eq!(rv, 0);
    println!("Found uptime {:?}.", info.uptime);
    assert!(info.uptime > 0);
    println!("Success.");
}
