use crate::action::AbilityOrigin;
use crate::card::{AbilityDef, AbilityTargetDef};
use crate::ids::CardDefinitionId;

use super::StackAbilityResolver;

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct FrozenActivatedAbility {
    pub(super) origin: AbilityOrigin,
    pub(super) definition: Option<Box<AbilityDef>>,
    pub(super) presentation_definition: CardDefinitionId,
    pub(super) text: Option<&'static str>,
    pub(super) target_defs: &'static [AbilityTargetDef],
    pub(super) resolver: StackAbilityResolver,
    /// The X chosen at activation, frozen alongside everything else the
    /// ability will resolve with.
    pub(super) x: u16,
}
