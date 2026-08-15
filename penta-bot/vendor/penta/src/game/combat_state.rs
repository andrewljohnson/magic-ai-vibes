use crate::ids::GameObjectId;

/// Which of the possible combat-damage steps is currently being processed.
///
/// The public protocol deliberately exposes both strike waves as
/// [`super::Step::CombatDamage`]. Keeping the first wave's participants here
/// lets the second wave follow the dynamic first strike/double strike
/// eligibility rules without changing that protocol.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) enum CombatDamageStage {
    NotStarted,
    Single,
    FirstStrike {
        strike_wave_combatants: Vec<GameObjectId>,
    },
    RegularAfterFirstStrike {
        strike_wave_combatants: Vec<GameObjectId>,
    },
}
