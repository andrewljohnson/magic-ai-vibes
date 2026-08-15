//! Snapshots for triggers waiting to be put on the stack.

use serde::{Deserialize, Serialize};

use super::{
    AbilityLocator, AbilitySourceSnapshot, TargetSelectionSnapshot, TriggerContextSnapshot,
};

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct PendingTriggerSnapshot {
    pub(in crate::game::state_checkpoint) id: u32,
    pub(in crate::game::state_checkpoint) source: AbilitySourceSnapshot,
    pub(in crate::game::state_checkpoint) ability: AbilityLocator,
    pub(in crate::game::state_checkpoint) definition: u16,
    pub(in crate::game::state_checkpoint) owner: usize,
    pub(in crate::game::state_checkpoint) controller: usize,
    pub(in crate::game::state_checkpoint) targets: Vec<TargetSelectionSnapshot>,
    pub(in crate::game::state_checkpoint) context: TriggerContextSnapshot,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(in crate::game::state_checkpoint) struct TriggerPlacementBatchSnapshot {
    pub(in crate::game::state_checkpoint) controller: usize,
    pub(in crate::game::state_checkpoint) triggers: Vec<PendingTriggerSnapshot>,
}
