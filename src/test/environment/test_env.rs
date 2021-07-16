fn main() {
    let var_1 = std::env::var("TESTING_ENV_VAR_1")
        .expect("Environment variable 'TESTING_ENV_VAR_1' not set");
    assert_eq!(var_1, "HELLO WORLD");

    let var_2 = std::env::var("TESTING_ENV_VAR_2")
        .expect("Environment variable 'TESTING_ENV_VAR_2' not set");
    assert_eq!(var_2, "SOMETHING");

    let ld_preload =
        std::env::var("LD_PRELOAD").expect("Environment variable 'LD_PRELOAD' not set");
    let ld_preload = ld_preload.split(':');
    assert!(ld_preload.last().unwrap() == "/my/custom/ld/preload/path.so");
}
