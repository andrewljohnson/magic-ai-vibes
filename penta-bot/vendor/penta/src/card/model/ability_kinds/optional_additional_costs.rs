//! Optional additional costs selected independently of how a spell is cast.
//!
//! Unlike an alternative cost, one of these adds to the spell's already
//! calculated cost and can therefore be combined with flashback, overload, or
//! any other legal way of casting it.

use crate::ids::{AbilityId, AdditionalCostId};

use super::super::{AdditionalCostDef, ManaCost};
use super::{SpellAdditionalCostDef, SpellResolutionDestinationDef};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum OptionalAdditionalCostKindDef {
    /// Buyback (CR 702.27): if this cost was paid, a resolving spell card goes
    /// to its owner's hand instead of its graveyard.
    Buyback,
    /// Replicate (CR 702.55): the cost may be paid any number of times, and
    /// what it buys is a copy of the spell for each payment. The copies come
    /// from a cast trigger the card prints beside this, which reads how many
    /// times the cost was paid.
    Replicate,
}

impl OptionalAdditionalCostKindDef {
    #[must_use]
    pub const fn label(self) -> &'static str {
        match self {
            Self::Buyback => "Buyback",
            Self::Replicate => "Replicate",
        }
    }

    /// Whether one cast may pay this cost more than once. Only replicate
    /// does: every other optional additional cost is paid once or not at all.
    #[must_use]
    pub const fn repeatable(self) -> bool {
        matches!(self, Self::Replicate)
    }
}

/// One named optional additional cost and the stack outcome it locks in.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct OptionalAdditionalCostAbilityDef {
    pub kind: OptionalAdditionalCostKindDef,
    pub mana_cost: Option<ManaCost>,
    pub additional_cost: Option<SpellAdditionalCostDef>,
    pub resolution_destination: SpellResolutionDestinationDef,
}

impl OptionalAdditionalCostAbilityDef {
    #[must_use]
    pub fn rules_text(self) -> String {
        match (self.kind, self.mana_cost) {
            (OptionalAdditionalCostKindDef::Buyback, Some(cost)) => format!(
                "Buyback {cost} (You may pay an additional {cost} as you cast this spell. If you \
                 do, put this card into your hand as it resolves.)"
            ),
            (OptionalAdditionalCostKindDef::Buyback, None) => "Buyback".into(),
            (OptionalAdditionalCostKindDef::Replicate, Some(cost)) => format!(
                "Replicate {cost} (When you cast this spell, copy it for each time you paid its \
                 replicate cost. You may choose new targets for the copies.)"
            ),
            (OptionalAdditionalCostKindDef::Replicate, None) => "Replicate".into(),
        }
    }

    #[must_use]
    pub fn additional_cost(self, ability: AbilityId) -> AdditionalCostDef {
        AdditionalCostDef {
            id: AdditionalCostId(ability.0),
            label: self.kind.label().into(),
            mana_cost: self.mana_cost,
            repeatable: self.kind.repeatable(),
        }
    }
}
