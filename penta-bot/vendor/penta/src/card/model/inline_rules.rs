use std::hash::{Hash, Hasher};

use super::{
    AbilityDef, CardRules, CardSupertype, CardType, CardTypeSet, ColorSet, CreatureStats,
    ManaColor, ObjectPredicateDef,
};

/// Compact rules shared by inline virtual-object and face-down values.
///
/// Abilities stay behind a slice because an ability can itself create another
/// inline value. Storing an [`AbilityDef`] here would make the declarative
/// effect schema recursively sized.
#[derive(Clone, Copy, Debug)]
pub(super) struct InlineRules {
    card_types: CardTypeSet,
    supertypes: [bool; CardSupertype::COUNT],
    subtypes: &'static [&'static str],
    colors: ColorSet,
    creature_stats: Option<CreatureStats>,
    abilities: &'static [AbilityDef],
    /// "Enchant creature" on an Aura that was never cast. A Role token is
    /// created already attached, so its restriction has to be printed on it
    /// rather than read off a spell's target. Held by reference to keep
    /// these values inside their inline size budget.
    enchant: Option<&'static ObjectPredicateDef>,
}

impl PartialEq for InlineRules {
    fn eq(&self, other: &Self) -> bool {
        self.card_types == other.card_types
            && self.supertypes == other.supertypes
            && self.subtypes == other.subtypes
            && self.colors == other.colors
            && self.creature_stats == other.creature_stats
            && self.enchant == other.enchant
            && std::ptr::eq(self.abilities, other.abilities)
    }
}

impl Eq for InlineRules {}

impl Hash for InlineRules {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.card_types.hash(state);
        self.supertypes.hash(state);
        self.subtypes.hash(state);
        self.colors.hash(state);
        self.creature_stats.hash(state);
        self.enchant.hash(state);
        self.abilities.as_ptr().hash(state);
        self.abilities.len().hash(state);
    }
}

impl InlineRules {
    pub(super) const fn new(
        card_types: CardTypeSet,
        subtypes: &'static [&'static str],
        colors: &'static [ManaColor],
        creature_stats: Option<CreatureStats>,
    ) -> Self {
        Self {
            card_types,
            supertypes: [false; CardSupertype::COUNT],
            subtypes,
            colors: ColorSet::from_colors(colors),
            creature_stats,
            abilities: &[],
            enchant: None,
        }
    }

    pub(super) const fn with_type(mut self, card_type: CardType) -> Self {
        self.card_types = self.card_types.with(card_type);
        self
    }

    pub(super) const fn with_supertype(mut self, supertype: CardSupertype) -> Self {
        self.supertypes[supertype.index()] = true;
        self
    }

    pub(super) const fn with_abilities(mut self, abilities: &'static [AbilityDef]) -> Self {
        self.abilities = abilities;
        self
    }

    pub(super) const fn with_enchant(mut self, object: &'static ObjectPredicateDef) -> Self {
        self.enchant = Some(object);
        self
    }

    pub(super) const fn subtypes(self) -> &'static [&'static str] {
        self.subtypes
    }

    pub(super) const fn materialize(self) -> CardRules {
        let rules = CardRules::from_inline_characteristics(
            self.card_types,
            self.supertypes,
            self.subtypes,
            self.colors,
            self.creature_stats,
            self.abilities,
        );
        match self.enchant {
            Some(object) => rules.enchanting(*object),
            None => rules,
        }
    }
}
