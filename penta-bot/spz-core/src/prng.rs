//! splitmix64 -- the SHARED explicit PRNG for determinized world
//! sampling. Mirrored byte-for-byte in Python (det_shared.py) so a
//! native-emitted training batch and a Python-emitted one draw the
//! identical hypothesis worlds on identical seeds, making row lockstep
//! bit-exact without reproducing CPython's Mersenne Twister.

pub struct SplitMix64(pub u64);

impl SplitMix64 {
    pub fn new(seed: u64) -> Self {
        SplitMix64(seed)
    }
    pub fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E3779B97F4A7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
        z ^ (z >> 31)
    }
    /// Uniform in [0, n) by Lemire-free modulo (matches the Python mirror).
    pub fn below(&mut self, n: usize) -> usize {
        (self.next_u64() % (n as u64)) as usize
    }
    /// In-place Fisher-Yates, from the end, index = below(i+1).
    pub fn shuffle<T>(&mut self, v: &mut [T]) {
        for i in (1..v.len()).rev() {
            let j = self.below(i + 1);
            v.swap(i, j);
        }
    }
}
