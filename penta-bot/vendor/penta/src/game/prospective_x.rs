//! Scoped scratch storage for the X a cast is being considered at.
//!
//! Target enumeration walks one X at a time through `&self` methods, so the
//! value it is currently considering needs interior mutability. A `Cell`
//! would be the obvious choice and is wrong here: `Game` reaches Python
//! through a pyo3 `#[pyclass]`, which requires `Send + Sync`, and a `Cell`
//! silently costs `Sync`. An atomic keeps the same single-threaded use while
//! leaving that contract intact. `src/game/tests/thread_safety.rs` holds the
//! assertion that catches a regression here in the native lane, rather than
//! several minutes later in the Python bindings job.

use std::sync::atomic::{AtomicU32, Ordering};

/// Out of range for the `u16` this stores, so it cannot collide with a real X.
const ABSENT: u32 = u32::MAX;

#[derive(Debug)]
pub(super) struct ProspectiveX(AtomicU32);

impl ProspectiveX {
    /// The X currently being considered, if a cast is mid-enumeration.
    pub(super) fn get(&self) -> Option<u16> {
        decode(self.0.load(Ordering::Relaxed))
    }

    /// Stores `value`, returning what was there so a caller can restore it.
    pub(super) fn replace(&self, value: Option<u16>) -> Option<u16> {
        decode(self.0.swap(encode(value), Ordering::Relaxed))
    }

    pub(super) fn set(&self, value: Option<u16>) {
        self.0.store(encode(value), Ordering::Relaxed);
    }
}

impl Default for ProspectiveX {
    fn default() -> Self {
        Self(AtomicU32::new(ABSENT))
    }
}

// Cloning a `Game` copies whatever X was being considered, matching what a
// `Cell` field would have done under `#[derive(Clone)]`.
impl Clone for ProspectiveX {
    fn clone(&self) -> Self {
        Self(AtomicU32::new(self.0.load(Ordering::Relaxed)))
    }
}

fn encode(value: Option<u16>) -> u32 {
    value.map_or(ABSENT, u32::from)
}

fn decode(raw: u32) -> Option<u16> {
    // Every stored value came from `encode`, so anything but `ABSENT` fits.
    (raw != ABSENT).then(|| u16::try_from(raw).expect("stored X came from a u16"))
}

#[cfg(test)]
mod tests {
    use super::ProspectiveX;

    #[test]
    fn absent_is_the_default_and_survives_a_round_trip() {
        let prospective = ProspectiveX::default();
        assert_eq!(prospective.get(), None);
        prospective.set(Some(0));
        assert_eq!(prospective.get(), Some(0));
        prospective.set(None);
        assert_eq!(prospective.get(), None);
    }

    #[test]
    fn replace_hands_back_the_previous_value() {
        let prospective = ProspectiveX::default();
        assert_eq!(prospective.replace(Some(3)), None);
        assert_eq!(prospective.replace(Some(u16::MAX)), Some(3));
        assert_eq!(prospective.replace(None), Some(u16::MAX));
        assert_eq!(prospective.get(), None);
    }

    #[test]
    fn a_clone_carries_the_value_being_considered() {
        let prospective = ProspectiveX::default();
        prospective.set(Some(7));
        assert_eq!(prospective.clone().get(), Some(7));
    }
}
