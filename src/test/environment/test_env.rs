fn main() {
    let var = std::env::var("TESTING_ENV_VAR_1")
        .expect("Environment variable 'TESTING_ENV_VAR_1' not set");
    assert_eq!(var, "HELLO WORLD");

    let var = std::env::var("TESTING_ENV_VAR_2")
        .expect("Environment variable 'TESTING_ENV_VAR_2' not set");
    assert_eq!(var, "SOMETHING");

    let var = std::env::var("TESTING_ENV_VAR_3")
        .expect("Environment variable 'TESTING_ENV_VAR_3' not set");
    assert_eq!(var, "");

    let var = std::env::var("TESTING_ENV_VAR_4")
        .expect("Environment variable 'TESTING_ENV_VAR_4' not set");
    assert_eq!(var, "X=Y");

    let var = std::env::var("TESTING_ENV_VAR_5")
        .expect("Environment variable 'TESTING_ENV_VAR_5' not set");
    assert_eq!(var, "X;Y");

    let ld_preload =
        std::env::var("LD_PRELOAD").expect("Environment variable 'LD_PRELOAD' not set");
    let mut ld_preload = ld_preload.split(':');
    assert!(ld_preload.next_back().unwrap() == "/my/custom/ld/preload/path.so");
}
