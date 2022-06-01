mod export {
    use std::ffi::CStr;

    use crate::core::support::configuration::ConfigOptions;

    #[no_mangle]
    pub extern "C" fn manager_saveProcessedConfigYaml(
        config: *const ConfigOptions,
        filename: *const libc::c_char,
    ) -> libc::c_int {
        let config = unsafe { config.as_ref() }.unwrap();
        let filename = unsafe { CStr::from_ptr(filename) }.to_str().unwrap();

        let file = match std::fs::File::create(&filename) {
            Ok(f) => f,
            Err(e) => {
                log::warn!("Could not create file {:?}: {}", filename, e);
                return 1;
            }
        };

        if let Err(e) = serde_yaml::to_writer(file, &config) {
            log::warn!(
                "Could not write processed config yaml to file {:?}: {}",
                filename,
                e
            );
            return 1;
        }

        return 0;
    }
}
