extern crate libc;

#[no_mangle]
pub extern fn rustmain() -> i32 {
    //println!("Hello, world!");
    unsafe {
        libc::write(1, "hello\n".as_ptr() as *const libc::c_void, 6);
    }
    0
}
