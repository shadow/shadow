#![cfg_attr(not(test), no_std)]

#[derive(vasi::VirtualAddressSpaceIndependent)]
pub struct VasiDeriveTest {
    pub x: i32,
}

#[no_mangle]
pub extern "C" fn test_entry_point() {

}

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    let _ = unsafe { linux_syscall::syscall!(linux_syscall::SYS_kill, 0, libc::SIGABRT) };
    unsafe { core::arch::asm!("ud2") }
    loop {}
}
