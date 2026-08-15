use crate::card::AbilityTargetDef;

use super::ScopedEffect;

/// The executable target/effect layout obtained after one concrete set of
/// spell modes has been selected. Building both vectors together keeps their
/// positional mapping atomic.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct SelectedSpellPlan {
    pub(super) target_defs: Vec<AbilityTargetDef>,
    pub(super) mode_effects: Vec<ScopedEffect>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) enum CastSourceZone {
    Hand,
    Graveyard,
}
