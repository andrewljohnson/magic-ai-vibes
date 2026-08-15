//! Stack-object snapshots.
//!
//! Split out of the parent module for the source-size budget; these are the
//! shapes describing what is on the stack and how each object got there.

use serde::{Deserialize, Serialize};

use super::{
    AbilityLocator, AbilityOriginSnapshot, AppliedEffectLocator, BasicLandTypeSnapshot,
    ManaSourceSnapshot, TargetSelectionSnapshot, TriggerContextSnapshot,
};

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::struct_excessive_bools)]
pub(in crate::game::state_checkpoint) struct StackSnapshot {
    pub(in crate::game::state_checkpoint) object_id: u32,
    pub(in crate::game::state_checkpoint) owner: usize,
    pub(in crate::game::state_checkpoint) ability_payload: Option<StackAbilitySnapshot>,
    pub(in crate::game::state_checkpoint) requires_retired_object: bool,
    pub(in crate::game::state_checkpoint) has_runtime_overrides: bool,
    pub(in crate::game::state_checkpoint) applied_effects: Vec<AppliedStackEffectSnapshot>,
    pub(in crate::game::state_checkpoint) text_changes: Vec<BasicLandTypeChangeSnapshot>,
    pub(in crate::game::state_checkpoint) colors: Option<[bool; 5]>,
    pub(in crate::game::state_checkpoint) cast_via_flashback: bool,
    pub(in crate::game::state_checkpoint) is_copy: bool,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct BasicLandTypeChangeSnapshot {
    pub(in crate::game::state_checkpoint) from: BasicLandTypeSnapshot,
    pub(in crate::game::state_checkpoint) to: BasicLandTypeSnapshot,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct StackAbilitySnapshot {
    pub(in crate::game::state_checkpoint) ability_locator: Option<AbilityLocator>,
    pub(in crate::game::state_checkpoint) origin: AbilityOriginSnapshot,
    pub(in crate::game::state_checkpoint) target_selections: Vec<TargetSelectionSnapshot>,
    pub(in crate::game::state_checkpoint) context: TriggerContextSnapshot,
    pub(in crate::game::state_checkpoint) mode_effects: Vec<ScopedEffectSnapshot>,
    pub(in crate::game::state_checkpoint) x: u16,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct DetachedStackSnapshot {
    pub(in crate::game::state_checkpoint) object_id: u32,
    pub(in crate::game::state_checkpoint) kind: StackObjectKindSnapshot,
    pub(in crate::game::state_checkpoint) definition: u16,
    pub(in crate::game::state_checkpoint) owner: usize,
    pub(in crate::game::state_checkpoint) source: Option<u32>,
    pub(in crate::game::state_checkpoint) ability_payload: Option<StackAbilitySnapshot>,
    pub(in crate::game::state_checkpoint) controller: usize,
    pub(in crate::game::state_checkpoint) signature: Option<CastSignatureSnapshot>,
    pub(in crate::game::state_checkpoint) chosen_permanents: Vec<u32>,
    pub(in crate::game::state_checkpoint) has_runtime_overrides: bool,
    pub(in crate::game::state_checkpoint) applied_effects: Vec<AppliedStackEffectSnapshot>,
    pub(in crate::game::state_checkpoint) text_changes: Vec<BasicLandTypeChangeSnapshot>,
    pub(in crate::game::state_checkpoint) colors: Option<[bool; 5]>,
    pub(in crate::game::state_checkpoint) cast_via_flashback: bool,
    pub(in crate::game::state_checkpoint) is_copy: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct AppliedStackEffectSnapshot {
    pub(in crate::game::state_checkpoint) source: Option<ManaSourceSnapshot>,
    pub(in crate::game::state_checkpoint) effect: AppliedEffectLocator,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) enum StackObjectKindSnapshot {
    Spell,
    ActivatedAbility,
    TriggeredAbility,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct CastSignatureSnapshot {
    pub(in crate::game::state_checkpoint) play_option: u8,
    pub(in crate::game::state_checkpoint) form: SpellFormSnapshot,
    pub(in crate::game::state_checkpoint) modes: Vec<u8>,
    pub(in crate::game::state_checkpoint) alternative_cost: Option<u8>,
    pub(in crate::game::state_checkpoint) additional_costs: Vec<u8>,
    pub(in crate::game::state_checkpoint) x: u16,
    pub(in crate::game::state_checkpoint) targets: Vec<TargetSelectionSnapshot>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(
    tag = "kind",
    rename_all = "camelCase",
    rename_all_fields = "camelCase"
)]
pub(in crate::game::state_checkpoint) enum SpellFormSnapshot {
    Part { part_id: u8 },
    Combined { part_ids: Vec<u8> },
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct ManaCostSnapshot {
    pub(in crate::game::state_checkpoint) generic: u16,
    pub(in crate::game::state_checkpoint) white: u16,
    pub(in crate::game::state_checkpoint) blue: u16,
    pub(in crate::game::state_checkpoint) black: u16,
    pub(in crate::game::state_checkpoint) red: u16,
    pub(in crate::game::state_checkpoint) green: u16,
    pub(in crate::game::state_checkpoint) hybrid: Vec<u16>,
    pub(in crate::game::state_checkpoint) variable_x: bool,
    pub(in crate::game::state_checkpoint) x_multiplier: u16,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct ScopedEffectSnapshot {
    pub(in crate::game::state_checkpoint) path: Vec<usize>,
    pub(in crate::game::state_checkpoint) target_base: usize,
}
