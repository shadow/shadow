use rand_xoshiro::Xoshiro256PlusPlus;

// we need this newtype for cbindgen
pub struct Random(pub Xoshiro256PlusPlus);

mod export {
    use super::*;
    use crate::utility::notnull::notnull_mut_debug;

    use std::convert::TryFrom;

    use rand::{Rng, RngCore, SeedableRng};

    #[no_mangle]
    pub unsafe extern "C" fn random_new(seed: u64) -> *mut Random {
        // Xoshiro256PlusPlus is not ideal when a seed with many zeros is used, but
        // 'seed_from_u64()' uses SplitMix64 to derive the actual seed, so we are okay here.
        let rng = Xoshiro256PlusPlus::seed_from_u64(seed);
        Box::into_raw(Box::new(Random(rng)))
    }

    #[no_mangle]
    pub extern "C" fn random_free(rng: *mut Random) {
        unsafe { Box::from_raw(notnull_mut_debug(rng)) };
    }

    /// Returns a pseudo-random integer in the range \[0, [`libc::RAND_MAX`]\].
    #[no_mangle]
    pub extern "C" fn random_rand(rng: *mut Random) -> u32 {
        let rng = &mut unsafe { rng.as_mut() }.unwrap().0;
        rng.gen_range(0..=u32::try_from(libc::RAND_MAX).unwrap())
    }

    /// Returns a pseudo-random float in the range \[0, 1).
    #[no_mangle]
    pub extern "C" fn random_nextDouble(rng: *mut Random) -> f64 {
        let rng = &mut unsafe { rng.as_mut() }.unwrap().0;
        rng.gen()
    }

    /// Returns a pseudo-random integer in the range \[0, [`u32::MAX`]\].
    #[no_mangle]
    pub extern "C" fn random_nextU32(rng: *mut Random) -> u32 {
        let rng = &mut unsafe { rng.as_mut() }.unwrap().0;
        rng.gen()
    }

    /// Fills the buffer with pseudo-random bytes.
    #[no_mangle]
    pub extern "C" fn random_nextNBytes(rng: *mut Random, buf: *mut u8, len: usize) {
        let rng = &mut unsafe { rng.as_mut() }.unwrap().0;

        let buf = unsafe { std::slice::from_raw_parts_mut(buf, len) };
        rng.fill_bytes(buf);
    }
}
