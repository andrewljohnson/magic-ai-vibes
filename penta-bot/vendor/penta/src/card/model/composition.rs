use crate::ids::{
    AdditionalCostId, AlternativeCostId, CardDefinitionId, CardPartId, PlayOptionId, TargetSlotId,
};

use super::{
    CardBehavior, CardEffectStatus, CardPart, CardPrinting, CardRules, CardSet, CardStructure,
    CardSupertype, CardType, DeclarativeAbilityDef, ImplementationStatus, ManaCost, ModeSetDef,
    PlayActionKind, PlayRestriction, PrintedManaCost, SpellForm, TargetSlotDef,
};

/// A named alternative to the cost supplied by a play option.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AlternativeCostDef {
    pub id: AlternativeCostId,
    pub label: String,
    pub mana_cost: ManaCost,
}

/// A named additional cost. Some additional costs are nonmana costs, so the
/// mana component is optional and the authoritative rules remain in `label`.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AdditionalCostDef {
    pub id: AdditionalCostId,
    pub label: String,
    pub mana_cost: Option<ManaCost>,
}

/// One legal way to play a card. This is distinct from rules-text modes and
/// from alternative/additional cost choices.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlayOptionDef {
    pub id: PlayOptionId,
    pub label: String,
    pub action: PlayActionKind,
    pub form: SpellForm,
    pub mana_cost: Option<ManaCost>,
    pub restriction: PlayRestriction,
    pub modes: Option<ModeSetDef>,
    pub targets: Vec<TargetSlotDef>,
    pub alternative_costs: Vec<AlternativeCostDef>,
    pub additional_costs: Vec<AdditionalCostDef>,
    pub effect_status: CardEffectStatus,
}

impl PlayOptionDef {
    #[must_use]
    pub fn cast(
        id: PlayOptionId,
        label: impl Into<String>,
        form: SpellForm,
        mana_cost: ManaCost,
        effect_status: CardEffectStatus,
    ) -> Self {
        Self::cast_with_printed_mana_cost(
            id,
            label,
            form,
            PrintedManaCost::Cost(mana_cost),
            effect_status,
        )
    }

    /// Defines a cast action without collapsing a nonexistent printed cost
    /// into `{0}`. A spell with `PrintedManaCost::None` ordinarily needs a
    /// separate casting permission or alternative cost before it is legal.
    #[must_use]
    pub fn cast_with_printed_mana_cost(
        id: PlayOptionId,
        label: impl Into<String>,
        form: SpellForm,
        printed_mana_cost: PrintedManaCost,
        effect_status: CardEffectStatus,
    ) -> Self {
        Self {
            id,
            label: label.into(),
            action: PlayActionKind::CastSpell,
            form,
            mana_cost: printed_mana_cost.as_option(),
            restriction: PlayRestriction::Normal,
            modes: None,
            targets: Vec::new(),
            alternative_costs: Vec::new(),
            additional_costs: Vec::new(),
            effect_status,
        }
    }

    #[must_use]
    pub fn play_land(
        id: PlayOptionId,
        label: impl Into<String>,
        part: CardPartId,
        effect_status: CardEffectStatus,
    ) -> Self {
        Self {
            id,
            label: label.into(),
            action: PlayActionKind::PlayLand,
            form: SpellForm::Part(part),
            mana_cost: None,
            restriction: PlayRestriction::Normal,
            modes: None,
            targets: Vec::new(),
            alternative_costs: Vec::new(),
            additional_costs: Vec::new(),
            effect_status,
        }
    }

    #[must_use]
    pub fn with_targets(mut self, targets: Vec<TargetSlotDef>) -> Self {
        self.targets = targets;
        self
    }

    #[must_use]
    pub fn with_modes(mut self, modes: ModeSetDef) -> Self {
        self.modes = Some(modes);
        self
    }

    /// Adds the printed alternative costs owned by alternative-casting
    /// clauses on `rules`. Existing manually authored generic alternatives
    /// remain intact.
    #[must_use]
    pub fn with_alternative_cast_costs(mut self, rules: &CardRules) -> Self {
        let card_mana_cost = self.mana_cost;
        self.alternative_costs.extend(
            rules
                .indexed_abilities()
                .filter_map(|ability| ability.alternative_cost(card_mana_cost)),
        );
        self
    }

    #[must_use]
    pub const fn restricted_to_hand(mut self) -> Self {
        self.restriction = PlayRestriction::FromHandOnly;
        self
    }
}

/// The structured portion of a card definition supplied by a set record.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CardComposition {
    pub parts: Vec<CardPart>,
    pub structure: CardStructure,
    pub play_options: Vec<PlayOptionDef>,
}

impl CardComposition {
    #[must_use]
    pub fn single(name: impl Into<String>, rules: CardRules) -> Self {
        let printed_mana_cost = rules.printed_mana_cost;
        let name = name.into();
        let is_land = rules.has_type(CardType::Land);
        let effect_status = match rules.implementation_status() {
            ImplementationStatus::MetadataOnly => CardEffectStatus::MetadataOnly,
            ImplementationStatus::Complete | ImplementationStatus::Partial => {
                CardEffectStatus::Implemented
            }
        };
        let part = CardPart::new(CardPartId::PRIMARY, name.clone(), rules);
        let mut option = if is_land {
            PlayOptionDef::play_land(
                PlayOptionId::DEFAULT,
                name,
                CardPartId::PRIMARY,
                effect_status,
            )
        } else {
            PlayOptionDef::cast_with_printed_mana_cost(
                PlayOptionId::DEFAULT,
                name,
                SpellForm::Part(CardPartId::PRIMARY),
                printed_mana_cost,
                effect_status,
            )
            .with_alternative_cast_costs(&rules)
        };
        if rules.play_restriction() != PlayRestriction::Normal {
            option.restriction = rules.play_restriction();
        }
        if let Some(modes) = rules.presentation_spell_modes() {
            option = option.with_modes(modes);
        }
        Self {
            parts: vec![part],
            structure: CardStructure::Single {
                main: CardPartId::PRIMARY,
            },
            play_options: vec![option],
        }
        .with_derived_spell_targets()
    }

    /// Derives nonmodal play-option target presentations from the spell
    /// clauses of the option's parts. Combined forms flatten their parts in
    /// printed order, assigning runtime slot IDs only after composition.
    ///
    /// A composition can still supply explicit presentation targets when it
    /// has no semantic spell clause. When the semantic predicate vocabulary
    /// is richer than the legacy presentation vocabulary, the projection is
    /// left empty and runtime target generation uses the semantic definition.
    #[must_use]
    pub(crate) fn with_derived_spell_targets(mut self) -> Self {
        for option in &mut self.play_options {
            if option.action != PlayActionKind::CastSpell
                || option.modes.is_some()
                || !option.targets.is_empty()
            {
                continue;
            }
            let part_ids = match &option.form {
                SpellForm::Part(part) => core::slice::from_ref(part),
                SpellForm::Combined(parts) => parts.as_slice(),
            };
            let derived = part_ids
                .iter()
                .try_fold(Vec::new(), |mut targets, part_id| {
                    let part = self.parts.iter().find(|part| part.id == *part_id)?;
                    let spell = part.rules.ability_clauses().iter().find_map(|ability| {
                        let DeclarativeAbilityDef::Spell(spell) = ability.definition else {
                            return None;
                        };
                        spell.modal().is_none().then_some(spell)
                    })?;
                    for target in spell.targets() {
                        let id = TargetSlotId::from_index(targets.len())?;
                        targets.push(target.presentation(id)?);
                    }
                    Some(targets)
                });
            if let Some(derived) = derived {
                option.targets = derived;
            }
        }
        self
    }
}

/// Canonical artwork metadata used when no exact printing is selected.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CardArt {
    pub scryfall_id: &'static str,
    pub artist: &'static str,
}

impl CardArt {
    #[must_use]
    pub const fn new(scryfall_id: &'static str, artist: &'static str) -> Self {
        Self {
            scryfall_id,
            artist,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CardDefinition {
    pub id: CardDefinitionId,
    pub name: String,
    pub art: Option<CardArt>,
    /// The canonical record's debut set within this catalog.
    ///
    /// Rules that care where a card debuted, such as City in a Bottle, use
    /// this field. Format legality instead considers every known `printing`.
    pub debut_set: CardSet,
    pub printings: Vec<CardPrinting>,
    /// Compatibility view of the primary/front part. Contextual rules should
    /// use `parts` once the game engine is part-aware.
    pub rules: CardRules,
    pub parts: Vec<CardPart>,
    pub structure: CardStructure,
    pub play_options: Vec<PlayOptionDef>,
}

impl CardDefinition {
    /// Creates a definition using the built-in metadata for `behavior`.
    #[must_use]
    pub fn new(
        id: CardDefinitionId,
        name: impl Into<String>,
        debut_set: CardSet,
        is_basic_land: bool,
        behavior: CardBehavior,
    ) -> Self {
        let name = name.into();
        let rules = if is_basic_land {
            (*behavior.rules()).with_supertype(CardSupertype::Basic)
        } else {
            *behavior.rules()
        };
        let composition = CardComposition::single(name.clone(), rules);
        Self {
            id,
            name,
            art: None,
            debut_set,
            printings: vec![CardPrinting::new(id, debut_set)],
            rules,
            parts: composition.parts,
            structure: composition.structure,
            play_options: composition.play_options,
        }
    }

    #[must_use]
    pub const fn is_basic_land(&self) -> bool {
        self.rules.has_type(CardType::Land) && self.rules.has_supertype(CardSupertype::Basic)
    }

    #[must_use]
    pub fn part(&self, id: CardPartId) -> Option<&CardPart> {
        self.parts.iter().find(|part| part.id == id)
    }

    #[must_use]
    pub fn play_option(&self, id: PlayOptionId) -> Option<&PlayOptionDef> {
        self.play_options.iter().find(|option| option.id == id)
    }

    /// Derives card-level coverage from every ordered clause on every part.
    /// A mix of complete and unimplemented parts is partial; a card is
    /// metadata-only only when every represented clause is unimplemented.
    #[must_use]
    pub fn implementation_status(&self) -> ImplementationStatus {
        let mut statuses = self
            .parts
            .iter()
            .map(|part| part.rules.implementation_status());
        statuses
            .next()
            .map_or(ImplementationStatus::Complete, |first| {
                statuses.fold(first, ImplementationStatus::combine)
            })
    }

    #[must_use]
    pub fn primary_part_id(&self) -> CardPartId {
        match &self.structure {
            CardStructure::Single { main } | CardStructure::AlternateSpell { main, .. } => *main,
            CardStructure::Split { parts, .. } => {
                parts.first().copied().unwrap_or(CardPartId::PRIMARY)
            }
            CardStructure::Flip { normal, .. } => *normal,
            CardStructure::DoubleFaced { front, .. } | CardStructure::MeldPart { front, .. } => {
                *front
            }
        }
    }

    /// The face on the other side of a double-faced card, or nothing when the
    /// card has only one side to present.
    #[must_use]
    pub fn other_face(&self, presented: CardPartId) -> Option<CardPartId> {
        let CardStructure::DoubleFaced { front, back, .. } = &self.structure else {
            return None;
        };
        if presented == *front {
            Some(*back)
        } else if presented == *back {
            Some(*front)
        } else {
            None
        }
    }

    #[must_use]
    pub fn primary_part(&self) -> Option<&CardPart> {
        self.part(self.primary_part_id())
    }
}
