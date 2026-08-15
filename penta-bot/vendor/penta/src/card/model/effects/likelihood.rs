//! The chance a randomized effect takes its success branch.
//!
//! It is a float behind a constructor so a card cannot state a probability
//! outside zero to one, and it carries its own equality and hashing because
//! floats do not have those for free.

/// A floating-point chance used by seeded randomized effects.
///
/// The value is finite and inclusive between `0.0` and `1.0`. The wrapper
/// keeps effect definitions const-friendly while giving their floating-point
/// likelihood a well-defined `Eq`/`Hash` contract.
#[derive(Clone, Copy, Debug)]
pub struct LikelihoodDef(f64);

impl LikelihoodDef {
    /// # Panics
    ///
    /// Panics when `likelihood` is not finite or is outside `0.0..=1.0`.
    #[must_use]
    pub const fn new(likelihood: f64) -> Self {
        assert!(
            likelihood >= 0.0 && likelihood <= 1.0,
            "likelihood must be finite and between 0.0 and 1.0"
        );
        let canonical = if likelihood.to_bits() == (-0.0_f64).to_bits() {
            0.0
        } else {
            likelihood
        };
        Self(canonical)
    }

    #[must_use]
    pub const fn value(self) -> f64 {
        self.0
    }
}

impl PartialEq for LikelihoodDef {
    fn eq(&self, other: &Self) -> bool {
        self.0.to_bits() == other.0.to_bits()
    }
}

impl Eq for LikelihoodDef {}

impl std::hash::Hash for LikelihoodDef {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.0.to_bits().hash(state);
    }
}
