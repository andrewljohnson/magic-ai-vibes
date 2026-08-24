//! The turn vocabulary an effect or a trigger can name.
//!
//! Kept apart from the effects themselves: what belongs here is the shape of
//! a turn, which effects refer to rather than contain.

/// A major turn phase that a resolving effect can insert.
///
/// This is intentionally narrower than [`TurnStepDef`]. Steps remain trigger
/// labels inside a phase, and the untap procedure remains part of ordinary
/// turn startup rather than an independently scheduled step.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum TurnPhaseDef {
    Combat,
    PostcombatMain,
}

/// Observable turn steps used by beginning/end-of-step trigger declarations.
/// Untap is an engine procedure before upkeep, and cleanup has no ordinary
/// priority window, so neither is an authored trigger label.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum TurnStepDef {
    Upkeep,
    Draw,
    PrecombatMain,
    BeginningOfCombat,
    DeclareAttackers,
    DeclareBlockers,
    CombatDamage,
    EndOfCombat,
    PostcombatMain,
    End,
    /// CR 514. Nothing is cast here and nobody receives priority unless
    /// something triggers, which is what "at the beginning of the next
    /// cleanup step" is buying.
    Cleanup,
}
