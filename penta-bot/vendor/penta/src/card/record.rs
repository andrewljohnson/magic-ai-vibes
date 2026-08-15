use super::{
    AbilityDef, CardArt, CardComposition, CardDefinition, CardPrinting, CardRules, CardSet,
};
use crate::game::CardAbilityResolver;
use crate::{AbilityId, CardDefinitionId, CardPartId, TargetSlotId};

type CompositionBuilder = fn() -> CardComposition;

/// Strategic meaning used to evaluate a card-owned ability without making its
/// runtime procedure part of the public rules model.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub(crate) enum AbilityPolicyHint {
    TargetPlayerSacrificesOneOfTwoPermanentPiles { target: TargetSlotId },
}

/// Internal runtime metadata attached to one printed ability.
///
/// `expected` guards the positional identity: if a card's abilities are
/// reordered without updating its binding, lookup fails instead of dispatching
/// the wrong procedure.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub(crate) struct CardAbilityBinding {
    pub(crate) part: CardPartId,
    pub(crate) ability: AbilityId,
    pub(crate) expected: AbilityDef,
    resolver: &'static CardAbilityResolver,
    policy_hint: Option<AbilityPolicyHint>,
}

impl CardAbilityBinding {
    #[must_use]
    #[allow(clippy::large_types_passed_by_value)]
    pub(crate) const fn new(
        part: CardPartId,
        ability: AbilityId,
        expected: AbilityDef,
        resolver: &'static CardAbilityResolver,
    ) -> Self {
        Self {
            part,
            ability,
            expected,
            resolver,
            policy_hint: None,
        }
    }

    #[must_use]
    pub(crate) const fn with_policy_hint(mut self, hint: AbilityPolicyHint) -> Self {
        self.policy_hint = Some(hint);
        self
    }

    #[must_use]
    pub(crate) const fn resolver(self) -> &'static CardAbilityResolver {
        self.resolver
    }

    #[must_use]
    pub(crate) const fn policy_hint(self) -> Option<AbilityPolicyHint> {
        self.policy_hint
    }
}

/// Internal source record from which the runtime catalog is built.
pub(super) struct CardRecord {
    pub(super) id: CardDefinitionId,
    pub(super) name: &'static str,
    pub(super) art: CardArt,
    pub(super) debut_set: CardSet,
    pub(super) rules: CardRules,
    composition: Option<CompositionBuilder>,
    pub(crate) ability_bindings: &'static [CardAbilityBinding],
}

impl CardRecord {
    #[allow(clippy::large_types_passed_by_value)]
    pub(super) const fn new(
        id: CardDefinitionId,
        name: &'static str,
        art: CardArt,
        debut_set: CardSet,
        rules: CardRules,
    ) -> Self {
        Self {
            id,
            name,
            art,
            debut_set,
            rules,
            composition: None,
            ability_bindings: &[],
        }
    }

    /// Supplies logical parts and play options for a structured or modal card.
    #[must_use]
    pub(super) const fn with_composition(mut self, builder: CompositionBuilder) -> Self {
        self.composition = Some(builder);
        self
    }

    /// Attaches card-owned runtime procedures without changing the public
    /// rules value produced by this record.
    #[must_use]
    pub(crate) const fn with_ability_bindings(
        mut self,
        bindings: &'static [CardAbilityBinding],
    ) -> Self {
        self.ability_bindings = bindings;
        self
    }

    pub(super) fn definition(&self) -> CardDefinition {
        let composition = self.composition.map_or_else(
            || CardComposition::single(self.name, self.rules),
            |builder| builder(),
        );
        CardDefinition {
            id: self.id,
            name: self.name.into(),
            art: Some(self.art),
            debut_set: self.debut_set,
            printings: vec![CardPrinting::new(self.id, self.debut_set)],
            rules: self.rules,
            parts: composition.parts,
            structure: composition.structure,
            play_options: composition.play_options,
        }
    }
}

/// A reprint or alternate-art printing whose rules come from `card`.
pub(super) struct PrintingRecord {
    pub(super) card: &'static CardRecord,
    pub(super) variant: u16,
}

impl PrintingRecord {
    /// Adds the default variant of `card` to another set.
    pub(super) const fn reprint(card: &'static CardRecord) -> Self {
        Self { card, variant: 0 }
    }

    /// Adds another distinguishable printing of `card` within the same set.
    pub(super) const fn alternate(card: &'static CardRecord, variant: u16) -> Self {
        assert!(variant > 0, "alternate printing variants start at one");
        Self { card, variant }
    }

    pub(super) const fn printing(&self, set: CardSet) -> CardPrinting {
        CardPrinting::with_variant(self.card.id, set, self.variant)
    }
}
