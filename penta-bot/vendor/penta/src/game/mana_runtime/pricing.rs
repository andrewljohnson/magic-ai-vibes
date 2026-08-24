//! What one payment is actually asked for, once the purpose behind it is
//! taken into account. A printed cost is the starting point rather than the
//! final word: a spell may demand a colour for its X, and a permission may
//! excuse the colours a creature's activation prints.

use super::super::{CardTypeSet, Game, ManaColor, ManaCost, ManaPaymentPurpose, fold_restricted_x};
use crate::card::AppliedRuleDef;

impl Game {
    /// The colour a spell's printed cost demands for X, if it prints such a
    /// restriction. Read from the compatibility primary-part view: no
    /// multi-part card prints one today.
    pub(in crate::game) fn x_spend_restriction(
        &self,
        purpose: &ManaPaymentPurpose,
    ) -> Option<ManaColor> {
        let ManaPaymentPurpose::Spell { definition, .. } = purpose else {
            return None;
        };
        self.catalog.get(*definition)?.rules.x_spend_restriction()
    }

    pub(in crate::game) fn restrict_x(
        &self,
        cost: ManaCost,
        x: u16,
        purpose: &ManaPaymentPurpose,
    ) -> (ManaCost, u16) {
        // The colour permission comes first so that a cost carrying both it
        // and "spend only black mana on X" still has to find black for the X
        // portion: the permission loosens what is printed, and the
        // restriction is then applied to the result.
        let cost = self.cost_after_color_permissions(cost, purpose);
        self.x_spend_restriction(purpose)
            .map_or((cost, x), |color| fold_restricted_x(cost, x, color))
    }

    /// The cost as this payment may actually pay it. Agatha's Soul Cauldron
    /// lets its controller treat any mana as any colour while activating an
    /// ability of a creature they control, which is the same thing as the
    /// coloured symbols in that cost having been generic all along.
    fn cost_after_color_permissions(
        &self,
        cost: ManaCost,
        purpose: &ManaPaymentPurpose,
    ) -> ManaCost {
        let ManaPaymentPurpose::Ability { source, .. } = purpose else {
            return cost;
        };
        let Some(permanent) = self
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == *source)
        else {
            return cost;
        };
        if !self
            .permanent_types(permanent)
            .is_some_and(CardTypeSet::is_creature)
        {
            return cost;
        }
        if self.player_rule_applies(
            permanent.controller,
            AppliedRuleDef::MaySpendManaAsAnyColorForCreatureAbilities,
        ) {
            cost.as_any_color()
        } else {
            cost
        }
    }
}
