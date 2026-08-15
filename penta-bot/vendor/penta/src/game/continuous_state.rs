use crate::action::AbilityOrigin;
use crate::card::{AbilityDef, AbilityPredicateDef, AppliedEffectDef};
use crate::ids::{AbilityId, CardDefinitionId, CardPartId, GameObjectId, GrantId, PlayerId};

use super::Permanent;

/// Timestamp shared by the continuous-effect slices currently modeled. Static
/// effects use their source permanent's battlefield timestamp; resolving
/// effects receive a fresh timestamp as they are created. Keeping this
/// independent from object identity lets a later layer evaluator preserve the
/// same ordering contract.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub(super) struct ContinuousEffectTimestamp(pub(super) u64);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) enum AbilityEffectExpiration {
    EndOfTurn,
    UpkeepOf(PlayerId),
    TurnOf { player: PlayerId, turn: u32 },
    Never,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct TemporaryGrantedAbility {
    pub(super) ability: AbilityDef,
    pub(super) source: GameObjectId,
    pub(super) source_definition: CardDefinitionId,
    pub(super) source_part: CardPartId,
    pub(super) source_ability: AbilityId,
    pub(super) grant: GrantId,
    pub(super) timestamp: ContinuousEffectTimestamp,
    pub(super) order: u16,
    pub(super) expiration: AbilityEffectExpiration,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct TemporaryRemovedAbilities {
    pub(super) predicate: AbilityPredicateDef,
    pub(super) timestamp: ContinuousEffectTimestamp,
    pub(super) order: u16,
    pub(super) expiration: AbilityEffectExpiration,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
// Ability definitions are immutable catalog values with static references.
// Keeping this operation Copy avoids allocation in the hot ability-layer walk.
#[allow(clippy::large_enum_variant)]
pub(super) enum AbilityLayerOperationKind {
    Add {
        origin: AbilityOrigin,
        ability: AbilityDef,
    },
    Remove(AbilityPredicateDef),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct AbilityLayerOperation {
    pub(super) timestamp: ContinuousEffectTimestamp,
    pub(super) order: u16,
    pub(super) kind: AbilityLayerOperationKind,
}

/// An ability granted to one non-battlefield object until cleanup. The object
/// identity naturally makes the grant end if that card changes zones.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct TemporaryAbilityGrant {
    pub(super) object: GameObjectId,
    pub(super) ability: AbilityDef,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct StaticAppliedEffect {
    pub(super) source: GameObjectId,
    pub(super) timestamp: ContinuousEffectTimestamp,
    pub(super) source_definition: CardDefinitionId,
    pub(super) source_part: CardPartId,
    pub(super) source_ability: AbilityId,
    pub(super) grant: Option<GrantId>,
    pub(super) effect: AppliedEffectDef,
}

pub(super) struct StaticEffectTraversal<'a> {
    pub(super) source: &'a Permanent,
    pub(super) source_timestamp: ContinuousEffectTimestamp,
    pub(super) source_definition: CardDefinitionId,
    pub(super) source_part: CardPartId,
    pub(super) source_ability: AbilityId,
    pub(super) affected: &'a Permanent,
    pub(super) prospective: Option<&'a Permanent>,
    pub(super) next_grant: usize,
}
