use super::{
    AbilityDef, AbilityId, AbilityOrigin, AbilityTargetDef, AbilityTargetPredicate, Action,
    AdditionalCostId, AlternativeCastAbilityDef, AlternativeCastKindDef, AlternativeCostId,
    CardBehavior, CardDefinition, CardDefinitionId, CardEffectStatus, CardInstance, CardPartId,
    CardType, CardTypeSet, CastChoices, CastSignature, CastSourceZone, ControlFlow,
    CostConfiguration, DeclarativeAbilityDef, DividedTotal, Game, GameObjectId, KeywordAbility,
    ManaCost, ManaPaymentPurpose, ModeId, PlayActionKind, PlayOptionDef, PlayOptionId,
    PlayRestriction, PlayerId, ScopedEffect, SelectedSpellPlan, StackAbilityPayload,
    StackAbilityResolver, Target, TargetSelection, TargetSlotDef, TargetSlotId, TriggerContext,
    ZoneKind, add_generic, add_mana_cost, configured_mana_cost, extra_target_cost,
    mode_id_selections, positive_compositions, reduce_generic, target_combinations,
};

impl Game {
    /// Every way to pay a spell's declarative additional cost. A spell with
    /// none has exactly one way to pay it: spend nothing. A spell with one it
    /// cannot afford has none at all, which is what stops it being offered.
    fn additional_cost_choices(
        &self,
        definition: &CardDefinition,
        card: &CardInstance,
        player: PlayerId,
    ) -> Vec<Vec<GameObjectId>> {
        let Some(cost) =
            definition
                .rules
                .ability_clauses()
                .iter()
                .find_map(|ability| match ability.definition {
                    DeclarativeAbilityDef::Spell(spell) if ability.is_executable() => {
                        spell.additional_cost()
                    }
                    _ => None,
                })
        else {
            return vec![Vec::new()];
        };
        let candidates: Vec<GameObjectId> = match cost.zone {
            ZoneKind::Battlefield => self
                .battlefield
                .iter()
                .filter(|permanent| {
                    permanent.controller == player
                        && self.trigger_object_matches(
                            cost.object,
                            &self.trigger_event_object(permanent),
                            permanent.card.id,
                            false,
                        )
                })
                .map(|permanent| permanent.card.id)
                .collect(),
            ZoneKind::Graveyard => self.players[player.index()]
                .graveyard
                .iter()
                .filter(|card| {
                    self.card_object_matches(cost.object, card, ZoneKind::Graveyard, card.id)
                })
                .map(|card| card.id)
                .collect(),
            // The card paying the cost cannot be the spell itself: it has
            // already left hand by the time the cost is paid.
            ZoneKind::Hand => self.players[player.index()]
                .hand
                .iter()
                .filter(|held| {
                    held.id != card.id
                        && self.card_object_matches(cost.object, held, ZoneKind::Hand, held.id)
                })
                .map(|held| held.id)
                .collect(),
            _ => Vec::new(),
        };
        if usize::from(cost.count) != 1 {
            // Only one object is chosen today; a cost naming more would need
            // every combination rather than every candidate.
            return Vec::new();
        }
        candidates.into_iter().map(|id| vec![id]).collect()
    }

    pub(super) fn add_land_actions(&self, player: PlayerId, actions: &mut Vec<Action>) {
        let state = &self.players[player.index()];
        if player != self.active_player
            || !self.step.is_main()
            || !self.stack.is_empty()
            || state.land_played_this_turn
        {
            return;
        }
        for card in &state.hand {
            let Some(definition) = self.catalog.get(card.definition) else {
                continue;
            };
            if self.play_is_prohibited(card, player) {
                continue;
            }
            actions.extend(
                definition
                    .play_options
                    .iter()
                    .filter(|option| option.action == PlayActionKind::PlayLand)
                    .filter(|option| match &option.form {
                        crate::card::SpellForm::Part(part) => definition
                            .part(*part)
                            .is_some_and(|part| part.rules.has_type(CardType::Land)),
                        crate::card::SpellForm::Combined(_) => false,
                    })
                    .map(|option| Action::PlayLand {
                        card: card.id,
                        option: option.id,
                    }),
            );
        }
    }

    #[allow(clippy::too_many_lines)]
    pub(super) fn add_spell_actions(&self, player: PlayerId, actions: &mut Vec<Action>) {
        let state = &self.players[player.index()];
        for (card, source_zone) in state
            .hand
            .iter()
            .map(|card| (card, CastSourceZone::Hand))
            .chain(
                state
                    .graveyard
                    .iter()
                    .map(|card| (card, CastSourceZone::Graveyard)),
            )
        {
            let Some(definition) = self.catalog.get(card.definition) else {
                continue;
            };
            if self.play_is_prohibited(card, player) {
                continue;
            }
            for option in definition
                .play_options
                .iter()
                .filter(|option| option.action == PlayActionKind::CastSpell)
            {
                if source_zone == CastSourceZone::Graveyard
                    && option.restriction == PlayRestriction::FromHandOnly
                {
                    continue;
                }
                if !self.play_timing_allows(option.restriction) {
                    continue;
                }
                // A declarative card intentionally has no custom behavior.
                // `Unsupported` is only a local neutral value for the legacy
                // helpers below; it is not stored as part of that card's rules.
                let behavior = Self::play_option_behavior(definition, option)
                    .unwrap_or(CardBehavior::Unsupported);
                let Some(types) = Self::play_option_types(definition, option) else {
                    continue;
                };
                // Metadata-only creatures retain baseline casting/combat. A
                // metadata-only noncreature spell or modal branch must not be
                // exposed as a legal action that would silently do nothing.
                if option.effect_status == CardEffectStatus::MetadataOnly && !types.is_creature() {
                    continue;
                }
                // A player stopped from casting noncreature spells this turn
                // keeps their creatures.
                if self.noncreature_casts_locked[player.index()] && !types.is_creature() {
                    continue;
                }
                let part_has_flash = match &option.form {
                    crate::card::SpellForm::Part(part) => {
                        definition.part(*part).is_some_and(|part| {
                            part.rules.has_executable_keyword(KeywordAbility::Flash)
                        })
                    }
                    crate::card::SpellForm::Combined(parts) => parts.iter().any(|part| {
                        definition.part(*part).is_some_and(|part| {
                            part.rules.has_executable_keyword(KeywordAbility::Flash)
                        })
                    }),
                };
                // A granted flash covers the next sorcery whenever it is
                // cast, so it only matters when the timing would refuse.
                let granted_flash = types.contains(CardType::Sorcery)
                    && self.sorcery_flash_grants[player.index()] > 0;
                if !types.contains(CardType::Instant)
                    && !part_has_flash
                    && !granted_flash
                    && (player != self.active_player
                        || !self.step.is_main()
                        || !self.stack.is_empty())
                {
                    continue;
                }
                let payment_purpose = ManaPaymentPurpose::Spell {
                    object: card.id,
                    definition: card.definition,
                    controller: player,
                    form: option.form.clone(),
                };

                for modes in Self::implemented_mode_selections(option) {
                    let declared_slots = Self::target_slots_for(option, &modes);
                    let _ = self.visit_cost_configurations(
                        definition,
                        card.id,
                        option,
                        source_zone,
                        |costs| {
                            let alternative_kind =
                                self.selected_alternative_kind(definition, option, card.id, &costs);
                            if alternative_kind == Some(AlternativeCastKindDef::Overload)
                                && !modes.is_empty()
                            {
                                return ControlFlow::Continue(());
                            }
                            let Some(cost) =
                                self.configured_cast_mana_cost(card.id, option, &costs)
                            else {
                                return ControlFlow::Continue(());
                            };
                            let max_x = if cost.variable_x {
                                self.maximum_x_for(player, cost, &payment_purpose)
                            } else {
                                0
                            };
                            for x in 0..=max_x {
                                let target_choices = if alternative_kind
                                    == Some(AlternativeCastKindDef::Overload)
                                {
                                    vec![Vec::new()]
                                } else if let Some((_, ability)) =
                                    Self::spell_ability(definition, option)
                                {
                                    let DeclarativeAbilityDef::Spell(spell) = ability.definition
                                    else {
                                        unreachable!("spell_ability returns a spell clause")
                                    };
                                    let Some(plan) = Self::selected_spell_plan(spell, &modes)
                                    else {
                                        continue;
                                    };
                                    self.legal_ability_target_selections(
                                        &plan.target_defs,
                                        player,
                                        card.id,
                                        TriggerContext::empty(),
                                        x,
                                    )
                                } else if Self::uses_legacy_behavior_targets(definition, option) {
                                    self.legacy_target_selections(behavior, player)
                                } else {
                                    self.legal_target_selections(&declared_slots, x)
                                };
                                for targets in &target_choices {
                                    let target_count = targets
                                        .iter()
                                        .map(|selection| selection.targets().len())
                                        .sum();
                                    let payable_cost = reduce_generic(
                                        add_generic(
                                            cost,
                                            extra_target_cost(definition, target_count),
                                        ),
                                        self.spell_cost_reduction(definition.id, player),
                                    );
                                    if !self.can_pay_cost_for(
                                        player,
                                        payable_cost,
                                        x,
                                        &payment_purpose,
                                    ) {
                                        continue;
                                    }
                                    let sacrifice_choices = if behavior
                                        == CardBehavior::GoblinGrenade
                                    {
                                        self.battlefield
                                            .iter()
                                            .filter(|permanent| {
                                                permanent.controller == player
                                                    && self.effective_rules(permanent).is_some_and(
                                                        |rules| rules.has_subtype("Goblin"),
                                                    )
                                            })
                                            .map(|permanent| vec![permanent.card.id])
                                            .collect()
                                    } else {
                                        self.additional_cost_choices(definition, card, player)
                                    };
                                    for sacrifices in sacrifice_choices {
                                        actions.push(Action::CastSpell {
                                            card: card.id,
                                            choices: CastChoices::new(option.id)
                                                .with_modes(modes.clone())
                                                .with_costs(costs.clone())
                                                .with_x(x)
                                                .with_targets(targets.clone()),
                                            sacrifices,
                                        });
                                    }
                                }
                            }
                            ControlFlow::Continue(())
                        },
                    );
                }
            }
        }
    }

    pub(super) fn play_option_types(
        definition: &CardDefinition,
        option: &PlayOptionDef,
    ) -> Option<CardTypeSet> {
        match &option.form {
            crate::card::SpellForm::Part(part) => {
                definition.part(*part).map(|part| part.rules.types())
            }
            crate::card::SpellForm::Combined(parts) => {
                let mut combined = CardTypeSet::empty();
                let mut found = false;
                for part in parts {
                    combined = combined.union(definition.part(*part)?.rules.types());
                    found = true;
                }
                found.then_some(combined)
            }
        }
    }

    pub(super) fn play_option_behavior(
        definition: &CardDefinition,
        option: &PlayOptionDef,
    ) -> Option<CardBehavior> {
        let first = match &option.form {
            crate::card::SpellForm::Part(part) => *part,
            crate::card::SpellForm::Combined(parts) => *parts.first()?,
        };
        definition
            .part(first)
            .and_then(|part| part.rules.special_behavior())
    }

    pub(super) fn spell_ability(
        definition: &CardDefinition,
        option: &PlayOptionDef,
    ) -> Option<(AbilityOrigin, AbilityDef)> {
        let crate::card::SpellForm::Part(part_id) = &option.form else {
            return None;
        };
        let part_id = *part_id;
        let part = definition.part(part_id)?;
        part.rules
            .indexed_abilities()
            .find(|attached| {
                attached.definition.is_executable()
                    && matches!(
                        attached.definition.definition,
                        DeclarativeAbilityDef::Spell(_)
                    )
            })
            .map(|attached| {
                (
                    AbilityOrigin::Printed {
                        definition: definition.id,
                        part: part_id,
                        ability: attached.id,
                    },
                    attached.definition,
                )
            })
    }

    pub(super) fn selected_spell_plan(
        spell: crate::card::SpellAbilityDef,
        selected_modes: &[ModeId],
    ) -> Option<SelectedSpellPlan> {
        let mut target_defs = spell.targets().to_vec();
        if target_defs.len() > usize::from(u8::MAX) + 1 {
            return None;
        }
        if spell.modal().is_none() {
            return selected_modes.is_empty().then_some(SelectedSpellPlan {
                target_defs,
                mode_effects: Vec::new(),
            });
        }
        let mut selected = selected_modes.to_vec();
        selected.sort_by_key(|mode| mode.index());
        let mut mode_effects = Vec::with_capacity(selected.len());
        for selected in selected {
            let mode = spell.mode(selected)?;
            let effect = mode.declarative_effect()?;
            let DeclarativeAbilityDef::Spell(mode_spell) = mode.definition else {
                return None;
            };
            let target_base = target_defs.len();
            let target_count = mode_spell.targets().len();
            if target_base.checked_add(target_count)? > usize::from(u8::MAX) + 1 {
                return None;
            }
            target_defs.extend_from_slice(mode_spell.targets());
            mode_effects.push(ScopedEffect {
                effect,
                target_base,
            });
        }
        Some(SelectedSpellPlan {
            target_defs,
            mode_effects,
        })
    }

    pub(super) fn alternative_cast_clause(
        definition: &CardDefinition,
        option: &PlayOptionDef,
        alternative: AlternativeCostId,
    ) -> Option<(AbilityOrigin, AbilityDef, AlternativeCastKindDef)> {
        let parts: &[CardPartId] = match &option.form {
            crate::card::SpellForm::Part(part) => std::slice::from_ref(part),
            crate::card::SpellForm::Combined(parts) => parts,
        };
        parts.iter().find_map(|part_id| {
            definition
                .part(*part_id)?
                .rules
                .indexed_abilities()
                .find_map(|attached| {
                    let DeclarativeAbilityDef::AlternativeCast(alternative_cast) =
                        attached.definition.definition
                    else {
                        return None;
                    };
                    (attached.alternative_cost_id() == Some(alternative)).then_some((
                        AbilityOrigin::Printed {
                            definition: definition.id,
                            part: *part_id,
                            ability: attached.id,
                        },
                        attached.definition,
                        alternative_cast.kind,
                    ))
                })
        })
    }

    pub(super) fn alternative_cast_ability(
        definition: &CardDefinition,
        option: &PlayOptionDef,
        alternative: AlternativeCostId,
    ) -> Option<(AbilityOrigin, AbilityDef, AlternativeCastKindDef)> {
        Self::alternative_cast_clause(definition, option, alternative)
            .filter(|(_, ability, _)| ability.is_executable())
    }

    pub(super) fn selected_alternative_kind(
        &self,
        definition: &CardDefinition,
        option: &PlayOptionDef,
        card: GameObjectId,
        costs: &CostConfiguration,
    ) -> Option<AlternativeCastKindDef> {
        let selected = costs.alternative()?;
        if Some(selected) == Self::temporary_alternative_cost_id(option)
            && self.granted_flashback(card, option).is_some()
        {
            return Some(AlternativeCastKindDef::Flashback);
        }
        Self::alternative_cast_ability(definition, option, selected).map(|(_, _, kind)| kind)
    }

    pub(super) fn temporary_alternative_cost_id(
        option: &PlayOptionDef,
    ) -> Option<AlternativeCostId> {
        (u8::MIN..=u8::MAX)
            .rev()
            .map(AlternativeCostId)
            .find(|candidate| {
                option
                    .alternative_costs
                    .iter()
                    .all(|cost| cost.id != *candidate)
            })
    }

    pub(super) fn granted_flashback(
        &self,
        card: GameObjectId,
        option: &PlayOptionDef,
    ) -> Option<(AlternativeCastAbilityDef, ManaCost)> {
        self.temporary_ability_grants
            .iter()
            .filter(|grant| grant.object == card)
            .find_map(|grant| {
                if !grant.ability.is_executable() {
                    return None;
                }
                let DeclarativeAbilityDef::AlternativeCast(alternative) = grant.ability.definition
                else {
                    return None;
                };
                (alternative.kind == AlternativeCastKindDef::Flashback)
                    .then(|| alternative.mana_cost.resolve(option.mana_cost))
                    .flatten()
                    .map(|mana_cost| (alternative, mana_cost))
            })
    }
    pub(super) fn spell_custom_followup(
        definition: &CardDefinition,
        option: &PlayOptionDef,
        primary: AbilityId,
    ) -> Option<CardBehavior> {
        let crate::card::SpellForm::Part(part_id) = &option.form else {
            return None;
        };
        definition
            .part(*part_id)?
            .rules
            .indexed_abilities()
            .find_map(|attached| {
                (attached.id != primary)
                    .then(|| attached.definition.custom_behavior())
                    .flatten()
            })
    }

    pub(super) fn frozen_spell_payload(
        &self,
        definition_id: CardDefinitionId,
        signature: &CastSignature,
    ) -> Option<StackAbilityPayload> {
        let definition = self.catalog.get(definition_id)?;
        let option = definition.play_option(signature.play_option())?;
        if let Some(selected) = signature.costs().alternative()
            && let Some((origin, ability, AlternativeCastKindDef::Overload)) =
                Self::alternative_cast_ability(definition, option, selected)
        {
            let DeclarativeAbilityDef::AlternativeCast(alternative_cast) = ability.definition
            else {
                unreachable!("alternative_cast_ability returns an alternative-cast clause")
            };
            return Some(StackAbilityPayload {
                origin,
                definition: Some(Box::new(ability)),
                presentation_definition: definition_id,
                text: alternative_cast.stack_text.or(Some(ability.text)),
                target_defs: Vec::new(),
                targets: signature.targets().to_vec(),
                context: TriggerContext::empty(),
                resolver: Self::ability_resolver(origin, &ability),
                condition: None,
                mode_effects: Vec::new(),
                x: signature.x(),
            });
        }
        let (origin, ability) = Self::spell_ability(definition, option)?;
        let AbilityOrigin::Printed {
            ability: ability_id,
            ..
        } = origin
        else {
            unreachable!("a printed spell clause has a printed origin")
        };
        let DeclarativeAbilityDef::Spell(spell) = ability.definition else {
            unreachable!("spell_ability returns a spell clause")
        };
        let followup = Self::spell_custom_followup(definition, option, ability_id);
        let plan = Self::selected_spell_plan(spell, signature.modes())
            .expect("validated modes select declared spell targets and branches");
        Some(StackAbilityPayload {
            origin,
            definition: Some(Box::new(ability)),
            presentation_definition: definition_id,
            text: Some(ability.text),
            target_defs: plan.target_defs,
            targets: signature.targets().to_vec(),
            context: TriggerContext::empty(),
            condition: None,
            resolver: match (ability.declarative_effect(), followup) {
                (Some(effect), Some(behavior)) => {
                    StackAbilityResolver::DeclarativeWithCustomFollowup {
                        effect: ScopedEffect::primary(effect),
                        behavior,
                    }
                }
                _ => Self::ability_resolver(origin, &ability),
            },
            mode_effects: plan.mode_effects,
            x: signature.x(),
        })
    }

    pub(super) fn uses_legacy_behavior_targets(
        definition: &CardDefinition,
        option: &PlayOptionDef,
    ) -> bool {
        matches!(
            (&definition.structure, &option.form),
            (
                crate::card::CardStructure::Single { main },
                crate::card::SpellForm::Part(part),
            ) if main == part
        ) && definition.play_options.len() == 1
            && option.id == PlayOptionId::DEFAULT
            && option.modes.is_none()
            && option.targets.is_empty()
            && Self::spell_ability(definition, option).is_none()
    }

    pub(super) fn implemented_mode_selections(option: &PlayOptionDef) -> Vec<Vec<ModeId>> {
        let Some(mode_set) = &option.modes else {
            return vec![Vec::new()];
        };
        let implemented = mode_set
            .modes
            .iter()
            .filter(|mode| mode.effect_status == CardEffectStatus::Implemented)
            .map(|mode| mode.id)
            .collect::<Vec<_>>();
        let mut implemented = implemented;
        implemented.sort_unstable();
        mode_id_selections(
            &implemented,
            usize::from(mode_set.minimum),
            usize::from(mode_set.maximum),
            mode_set.may_repeat,
        )
    }

    pub(super) fn target_slots_for(option: &PlayOptionDef, modes: &[ModeId]) -> Vec<TargetSlotDef> {
        let mut slots = option.targets.clone();
        if let Some(mode_set) = &option.modes {
            for mode in modes {
                if let Some(mode) = mode_set
                    .modes
                    .iter()
                    .find(|candidate| candidate.id == *mode)
                {
                    slots.extend(mode.targets.clone());
                }
            }
        }
        for (index, slot) in slots.iter_mut().enumerate() {
            slot.id = TargetSlotId::from_index(index)
                .expect("one play option presents at most 256 target slots");
        }
        slots
    }

    pub(super) fn visit_cost_configurations(
        &self,
        definition: &CardDefinition,
        card: GameObjectId,
        option: &PlayOptionDef,
        source_zone: CastSourceZone,
        mut visitor: impl FnMut(CostConfiguration) -> ControlFlow<()>,
    ) -> ControlFlow<()> {
        let mut selected_additional = Vec::with_capacity(option.additional_costs.len());
        if source_zone == CastSourceZone::Hand
            && Self::visit_additional_cost_configurations(
                option,
                None,
                option.additional_costs.len(),
                &mut selected_additional,
                &mut visitor,
            )
            .is_break()
        {
            return ControlFlow::Break(());
        }
        for cost in &option.alternative_costs {
            let kind = match Self::alternative_cast_clause(definition, option, cost.id) {
                Some((_, ability, kind)) if ability.is_executable() => Some(kind),
                Some(_) => continue,
                None => None,
            };
            let available = match (source_zone, kind) {
                (CastSourceZone::Hand, Some(AlternativeCastKindDef::Flashback))
                | (
                    CastSourceZone::Graveyard,
                    Some(AlternativeCastKindDef::Overload | AlternativeCastKindDef::Miracle) | None,
                ) => false,
                (CastSourceZone::Hand, Some(AlternativeCastKindDef::Overload) | None)
                | (CastSourceZone::Graveyard, Some(AlternativeCastKindDef::Flashback)) => true,
                // Only in the window the draw opened, and only for the card
                // that was drawn.
                (CastSourceZone::Hand, Some(AlternativeCastKindDef::Miracle)) => {
                    self.miracle_window == Some(card)
                }
            };
            if available
                && Self::visit_additional_cost_configurations(
                    option,
                    Some(cost.id),
                    option.additional_costs.len(),
                    &mut selected_additional,
                    &mut visitor,
                )
                .is_break()
            {
                return ControlFlow::Break(());
            }
        }
        if source_zone == CastSourceZone::Graveyard
            && self.granted_flashback(card, option).is_some()
            && let Some(granted) = Self::temporary_alternative_cost_id(option)
            && Self::visit_additional_cost_configurations(
                option,
                Some(granted),
                option.additional_costs.len(),
                &mut selected_additional,
                &mut visitor,
            )
            .is_break()
        {
            return ControlFlow::Break(());
        }

        ControlFlow::Continue(())
    }

    pub(super) fn visit_additional_cost_configurations(
        option: &PlayOptionDef,
        alternative: Option<AlternativeCostId>,
        remaining: usize,
        selected_reversed: &mut Vec<AdditionalCostId>,
        visitor: &mut impl FnMut(CostConfiguration) -> ControlFlow<()>,
    ) -> ControlFlow<()> {
        let Some(index) = remaining.checked_sub(1) else {
            let additional = selected_reversed.iter().rev().copied().collect();
            return visitor(CostConfiguration::new(alternative, additional));
        };

        if Self::visit_additional_cost_configurations(
            option,
            alternative,
            index,
            selected_reversed,
            visitor,
        )
        .is_break()
        {
            return ControlFlow::Break(());
        }
        selected_reversed.push(option.additional_costs[index].id);
        let result = Self::visit_additional_cost_configurations(
            option,
            alternative,
            index,
            selected_reversed,
            visitor,
        );
        selected_reversed.pop();
        result
    }

    pub(super) fn configured_cast_mana_cost(
        &self,
        card: GameObjectId,
        option: &PlayOptionDef,
        configuration: &CostConfiguration,
    ) -> Option<ManaCost> {
        let granted = Self::temporary_alternative_cost_id(option);
        let granted_flashback = (configuration.alternative().is_some()
            && configuration.alternative() == granted)
            .then(|| self.granted_flashback(card, option))
            .flatten();
        let mut cost = granted_flashback.map_or_else(
            || configured_mana_cost(option, configuration),
            |(_, mana_cost)| Some(mana_cost),
        )?;
        // `configured_mana_cost` already included additional costs for every
        // printed alternative and the normal cost. Runtime-granted
        // alternatives need them folded in here.
        if granted_flashback.is_some() {
            for selected in configuration.additional() {
                let additional = option
                    .additional_costs
                    .iter()
                    .find(|candidate| candidate.id == *selected)?;
                if let Some(mana) = additional.mana_cost {
                    cost = add_mana_cost(cost, mana);
                }
            }
        }
        Some(cost)
    }

    pub(super) fn legacy_target_selections(
        &self,
        behavior: CardBehavior,
        player: PlayerId,
    ) -> Vec<Vec<TargetSelection>> {
        self.legal_target_lists(behavior, player, None)
            .into_iter()
            .map(|targets| {
                if targets.is_empty() {
                    Vec::new()
                } else {
                    vec![TargetSelection::new(TargetSlotId(0), targets)]
                }
            })
            .collect()
    }

    pub(super) fn legal_target_selections(
        &self,
        slots: &[TargetSlotDef],
        x: u16,
    ) -> Vec<Vec<TargetSelection>> {
        let mut selections = vec![Vec::new()];
        for slot in slots {
            let candidates = self.targets_matching(slot.predicate);
            let mut choices = Vec::new();
            if let Some(total) = slot.divided_total {
                let total = match total {
                    DividedTotal::Fixed(total) => total,
                    DividedTotal::ChosenX => u8::try_from(x).unwrap_or(u8::MAX),
                };
                // Every chosen target takes at least one, so the number of
                // targets follows from how the total is split.
                for count in 1..=usize::from(total).min(candidates.len()) {
                    for targets in target_combinations(&candidates, count) {
                        for amounts in positive_compositions(total, count) {
                            choices.push(TargetSelection::divided(
                                slot.id,
                                targets.clone(),
                                amounts,
                            ));
                        }
                    }
                }
                let mut combined = Vec::new();
                for prefix in &selections {
                    for choice in &choices {
                        let mut selected = prefix.clone();
                        selected.push(choice.clone());
                        combined.push(selected);
                    }
                }
                selections = combined;
                continue;
            }
            for count in slot.minimum..=slot.maximum {
                choices.extend(
                    target_combinations(&candidates, usize::from(count))
                        .into_iter()
                        .map(|targets| TargetSelection::new(slot.id, targets)),
                );
            }
            let mut combined = Vec::new();
            for prefix in &selections {
                for choice in &choices {
                    let mut selected = prefix.clone();
                    selected.push(choice.clone());
                    combined.push(selected);
                }
            }
            selections = combined;
        }
        selections
    }

    pub(super) fn legal_ability_target_selections(
        &self,
        slots: &[AbilityTargetDef],
        controller: PlayerId,
        source: GameObjectId,
        context: TriggerContext,
        x: u16,
    ) -> Vec<Vec<TargetSelection>> {
        let mut selections = vec![Vec::new()];
        for (index, slot) in slots.iter().enumerate() {
            let id = TargetSlotId::from_index(index)
                .expect("validated ability targets fit the runtime slot space");
            // A slot that reads an earlier slot's choice has to be enumerated
            // once per prefix, because its candidates are different for each.
            if let AbilityTargetPredicate::ControlledByTargetOf {
                object,
                slot: other,
            } = slot.predicate
            {
                let other = TargetSlotId::from_index(other.index())
                    .expect("validated dependent target fits the runtime slot space");
                let mut combined = Vec::new();
                for prefix in &selections {
                    let candidates = prefix
                        .iter()
                        .find(|selection: &&TargetSelection| selection.slot() == other)
                        .and_then(|selection| selection.targets().first().copied())
                        .and_then(|target| match target {
                            Target::Player(player) => Some(player),
                            Target::Permanent(id) | Target::Card(id) | Target::Spell(id) => {
                                self.current_or_last_known_controller(id)
                            }
                        })
                        .map_or_else(Vec::new, |owner| {
                            self.battlefield
                                .iter()
                                .filter(|permanent| permanent.controller == owner)
                                .filter(|permanent| {
                                    self.trigger_object_matches(
                                        object,
                                        &self.trigger_event_object(permanent),
                                        source,
                                        false,
                                    ) && self
                                        .permanent_can_be_targeted_by(permanent, controller, source)
                                })
                                .map(|permanent| Target::Permanent(permanent.card.id))
                                .collect::<Vec<_>>()
                        });
                    for count in slot.minimum..=slot.maximum {
                        for targets in target_combinations(&candidates, usize::from(count)) {
                            let mut selected = prefix.clone();
                            selected.push(TargetSelection::new(id, targets));
                            combined.push(selected);
                        }
                    }
                }
                selections = combined;
                continue;
            }
            let candidates =
                self.ability_targets_matching(slot.predicate, controller, source, context);
            let mut choices = Vec::new();
            if let Some(total) = slot.divided_total {
                let total = match total {
                    DividedTotal::Fixed(total) => total,
                    DividedTotal::ChosenX => u8::try_from(x).unwrap_or(u8::MAX),
                };
                // Every chosen target takes at least one, so the number of
                // targets follows from how the total is split.
                for count in 1..=usize::from(total).min(candidates.len()) {
                    for targets in target_combinations(&candidates, count) {
                        for amounts in positive_compositions(total, count) {
                            choices.push(TargetSelection::divided(id, targets.clone(), amounts));
                        }
                    }
                }
                let mut combined = Vec::new();
                for prefix in &selections {
                    for choice in &choices {
                        let mut selected = prefix.clone();
                        selected.push(choice.clone());
                        combined.push(selected);
                    }
                }
                selections = combined;
                continue;
            }
            for count in slot.minimum..=slot.maximum {
                choices.extend(
                    target_combinations(&candidates, usize::from(count))
                        .into_iter()
                        .map(|targets| TargetSelection::new(id, targets)),
                );
            }
            let mut combined = Vec::new();
            for prefix in &selections {
                for choice in &choices {
                    let mut selected = prefix.clone();
                    selected.push(choice.clone());
                    combined.push(selected);
                }
            }
            selections = combined;
        }
        selections
    }
}
