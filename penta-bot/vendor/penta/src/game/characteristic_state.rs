use crate::action::AbilityOrigin;
use crate::card::{AbilityDef, BasicLandType, CardTypeSet, KeywordAbility};
use crate::ids::{CardDefinitionId, CardPartId, MeldRecipeId};

use super::TriggerEventObject;

/// Where this object's copiable characteristics come from. This deliberately
/// does not follow physical backing: a copy can have characteristics with no
/// card, while a future meld result can be backed by two cards without being
/// the printed definition of either one.
#[derive(Clone, Debug, Eq, PartialEq)]
#[allow(dead_code)]
pub(super) enum CharacteristicSource {
    Card(CardDefinitionId),
    Copy(CardDefinitionId),
    Ability(CardDefinitionId),
    Meld(MeldRecipeId),
}

/// One indefinite text-changing effect in layer 3. These effects belong to
/// the object, are applied in timestamp order, and are deliberately excluded
/// from its copiable values.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct BasicLandTypeChange {
    pub(super) from: BasicLandType,
    pub(super) to: BasicLandType,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) enum LandTypeOperation {
    SetTo(&'static [BasicLandType]),
    Add(&'static [BasicLandType]),
}

/// An ability added as an exception while copying an object. Unlike an
/// ordinary granted ability, this becomes part of the resulting object's
/// copiable values and can therefore be copied again.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct CopiableAbility {
    pub(super) origin: AbilityOrigin,
    pub(super) definition: AbilityDef,
}

/// The compact copiable-value snapshot needed by the copy effects currently
/// supported by the engine. The catalog source supplies all ordinary printed
/// characteristics; copy-process exceptions are frozen beside it.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct CopiableCharacteristics {
    pub(super) base: (CardDefinitionId, CardPartId),
    pub(super) added_types: CardTypeSet,
    pub(super) added_abilities: Vec<CopiableAbility>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct EffectiveAbility {
    pub(super) origin: AbilityOrigin,
    pub(super) ability: AbilityDef,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct PermanentLastKnownInformation {
    pub(super) power: Option<i16>,
    pub(super) toughness: Option<i16>,
    pub(super) mana_value: u16,
    pub(super) keywords: Vec<KeywordAbility>,
}

/// Characteristics and abilities frozen immediately before a permanent exits
/// the battlefield. Every member of a simultaneous exit batch is snapshotted
/// before any member is removed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct BattlefieldExitSnapshot {
    pub(super) object: TriggerEventObject,
    pub(super) abilities: Vec<EffectiveAbility>,
    pub(super) last_known: PermanentLastKnownInformation,
}
