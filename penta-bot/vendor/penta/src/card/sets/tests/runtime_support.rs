mod nested_definitions;
mod stack_effects;
mod static_effects;

pub(super) use static_effects::shared_static_effect;

pub(super) use nested_definitions::*;
pub(super) use stack_effects::shared_stack_effect;

use crate::Game;
use crate::card::{ActivatedAbilityDef, ReplacementConditionDef, SpellAdditionalCostDef};

use super::*;

pub(super) fn shared_object_predicate(predicate: ObjectPredicateDef) -> bool {
    match predicate {
        ObjectPredicateDef::All(predicates) | ObjectPredicateDef::AnyOf(predicates) => {
            predicates.iter().copied().all(shared_object_predicate)
        }
        ObjectPredicateDef::Not(predicate) => shared_object_predicate(*predicate),
        ObjectPredicateDef::Special(_) => false,
        ObjectPredicateDef::Any
        | ObjectPredicateDef::Source
        | ObjectPredicateDef::Token
        | ObjectPredicateDef::HasType(_)
        | ObjectPredicateDef::HasAnyBasicLandType(_)
        | ObjectPredicateDef::Spell
        | ObjectPredicateDef::NoncreatureSpell
        | ObjectPredicateDef::Color(_)
        | ObjectPredicateDef::ColorCount(_)
        | ObjectPredicateDef::Subtype(_)
        | ObjectPredicateDef::ManaValueAtMost(_)
        | ObjectPredicateDef::ManaValueEqualTo(_)
        | ObjectPredicateDef::ManaValueAtMostValue(_)
        | ObjectPredicateDef::PowerAtLeast(_)
        | ObjectPredicateDef::PowerExactly(_)
        | ObjectPredicateDef::ToughnessExactly(_)
        | ObjectPredicateDef::ToughnessLessThan(_)
        | ObjectPredicateDef::PowerGreaterThan(_)
        | ObjectPredicateDef::PowerLessThan(_)
        | ObjectPredicateDef::ToughnessGreaterThan(_)
        | ObjectPredicateDef::ControlledBy(_)
        | ObjectPredicateDef::Supertype(_)
        | ObjectPredicateDef::DebutSet(_)
        | ObjectPredicateDef::SharesNameWithSource
        | ObjectPredicateDef::AttackingOrBlocking
        | ObjectPredicateDef::HasKeyword(_)
        | ObjectPredicateDef::HasCounter(_)
        | ObjectPredicateDef::HasNonManaActivatedAbility
        | ObjectPredicateDef::Tapped
        | ObjectPredicateDef::Attacking
        | ObjectPredicateDef::Blocking
        | ObjectPredicateDef::AttachedToSource
        | ObjectPredicateDef::BlockedBySource
        | ObjectPredicateDef::Enchanted
        | ObjectPredicateDef::AttachedTo(_)
        | ObjectPredicateDef::AttackedThisTurn => true,
    }
}

pub(super) fn shared_effect_recipient(recipient: EffectRecipientDef) -> bool {
    match recipient {
        EffectRecipientDef::MatchingObjects { object, zones, .. } => {
            !zones.is_empty()
                && zones.iter().all(|zone| {
                    matches!(
                        zone,
                        ZoneKind::Battlefield
                            | ZoneKind::Stack
                            | ZoneKind::Library
                            | ZoneKind::Hand
                            | ZoneKind::Graveyard
                            | ZoneKind::Exile
                            | ZoneKind::Command
                    )
                })
                && shared_object_predicate(object)
        }
        // The sweep is over the battlefield, so only the predicate
        // needs checking.
        EffectRecipientDef::ObjectsControlledByTarget { object, .. }
        | EffectRecipientDef::ObjectsOwnedByTarget { object, .. } => {
            shared_object_predicate(object)
        }
        EffectRecipientDef::CardsOwnedByTarget { object, zones, .. } => {
            !zones.is_empty()
                && zones.iter().all(|zone| {
                    matches!(
                        zone,
                        ZoneKind::Library | ZoneKind::Hand | ZoneKind::Graveyard | ZoneKind::Exile
                    )
                })
                && shared_object_predicate(object)
        }
        EffectRecipientDef::ObjectsSharingNameWithTarget(_)
        | EffectRecipientDef::Source
        | EffectRecipientDef::ChosenPermanent(_)
        | EffectRecipientDef::AttachedPermanent
        | EffectRecipientDef::Controller
        | EffectRecipientDef::Opponent
        | EffectRecipientDef::EachPlayer
        | EffectRecipientDef::Target(_)
        | EffectRecipientDef::ControllerOfTarget(_)
        | EffectRecipientDef::TriggeringObject
        | EffectRecipientDef::ControllerOfTriggeringObject
        | EffectRecipientDef::ControllerOfAttachedPermanent
        | EffectRecipientDef::EventPlayer => true,
    }
}

pub(super) fn shared_keyword(keyword: KeywordAbility) -> bool {
    matches!(
        keyword,
        KeywordAbility::Flying
            | KeywordAbility::Trample
            | KeywordAbility::Haste
            | KeywordAbility::FirstStrike
            | KeywordAbility::DoubleStrike
            | KeywordAbility::Vigilance
            | KeywordAbility::Defender
            | KeywordAbility::Deathtouch
            | KeywordAbility::Lifelink
            | KeywordAbility::Reach
            | KeywordAbility::Flash
            | KeywordAbility::Hexproof
            | KeywordAbility::Shroud
            | KeywordAbility::Intimidate
            | KeywordAbility::Undying
            | KeywordAbility::Indestructible
            | KeywordAbility::Landwalk(_)
            | KeywordAbility::LegendaryLandwalk
            | KeywordAbility::AttacksEachCombatIfAble
            | KeywordAbility::Unleash
            | KeywordAbility::ProtectionFrom(_)
    )
}

pub(super) fn shared_zone_move_cause(cause: ZoneMoveCauseDef) -> bool {
    matches!(
        cause,
        ZoneMoveCauseDef::Any
            | ZoneMoveCauseDef::EffectControlledBy(
                PlayerRelation::Any
                    | PlayerRelation::You
                    | PlayerRelation::Opponent
                    | PlayerRelation::ActivePlayer
                    | PlayerRelation::NonactivePlayer
            )
    )
}

pub(super) fn shared_cannot_be_countered_effect(effect: AppliedEffectDef) -> bool {
    match effect {
        AppliedEffectDef::Composite(effects) => {
            !effects.is_empty()
                && effects
                    .iter()
                    .copied()
                    .all(shared_cannot_be_countered_effect)
        }
        AppliedEffectDef::CannotBeCountered | AppliedEffectDef::CannotBeEnchanted => true,
        AppliedEffectDef::ModifyPowerToughness { .. }
        | AppliedEffectDef::DoesNotUntapDuringUntapStep
        | AppliedEffectDef::MayChooseNotToUntap
        | AppliedEffectDef::CannotBlock
        | AppliedEffectDef::CannotAttack
        | AppliedEffectDef::CannotBeBlocked
        | AppliedEffectDef::CannotBecomeEnchanted
        | AppliedEffectDef::CannotChangeController
        | AppliedEffectDef::RemainsAttachedThroughProtection
        | AppliedEffectDef::CannotBeBlockedBy(_)
        | AppliedEffectDef::CanBlockOnly(_)
        | AppliedEffectDef::PreventDamageFrom(_)
        | AppliedEffectDef::PreventCombatDamageFrom(_)
        | AppliedEffectDef::PreventCombatDamage
        | AppliedEffectDef::PreventCombatDamageDealtBy
        | AppliedEffectDef::AddLandTypes(_)
        | AppliedEffectDef::SetLandTypes(_)
        | AppliedEffectDef::RemoveAbilities(_)
        | AppliedEffectDef::Animate(_)
        | AppliedEffectDef::GrantAbility(_)
        | AppliedEffectDef::Special(_) => false,
    }
}

pub(super) fn shared_mana_effect(effect: EffectDef, choices_are_supported: bool) -> bool {
    let EffectDef::AddMana(mana) = effect else {
        return false;
    };
    let selection_is_supported = match mana.mana {
        ManaSelectionDef::One(_) => true,
        ManaSelectionDef::Choice(colors) => choices_are_supported && !colors.is_empty(),
    };
    selection_is_supported
        && mana.amount > 0
        && mana
            .restrictions
            .iter()
            .copied()
            .all(|restriction| match restriction {
                ManaRestrictionDef::CastSpell(object) => shared_object_predicate(object),
                ManaRestrictionDef::CastCreatureSpellOfChosenType => true,
                ManaRestrictionDef::ActivateAbility(_) | ManaRestrictionDef::Special(_) => false,
            })
        && mana.spend_effects.iter().copied().all(|effect| {
            let ManaSpendEffectDef::ApplyToPaidSpell(effect) = effect else {
                return false;
            };
            shared_cannot_be_countered_effect(effect)
        })
}

pub(super) fn shared_resolving_apply(
    recipient: EffectRecipientDef,
    effect: AppliedEffectDef,
    duration: EffectDurationDef,
) -> bool {
    // Resolved ability additions and removals share one duration-aware
    // operation path. Other applied effects still end with the turn.
    let ability_change = resolving_effect_is_only_ability_changes(effect);
    // "For as long as this artifact remains tapped" is recorded against its
    // source rather than a deadline, which only the stat modification does.
    let stat_change = matches!(effect, AppliedEffectDef::ModifyPowerToughness { .. });
    let duration_is_supported = duration == EffectDurationDef::UntilEndOfTurn
        || duration == EffectDurationDef::UntilYourNextUpkeep && ability_change
        || matches!(
            duration,
            EffectDurationDef::UntilYourNextTurn | EffectDurationDef::Permanent
        ) && ability_change
        || duration == EffectDurationDef::WhileSourceTapped && stat_change;
    if !duration_is_supported || !shared_effect_recipient(recipient) {
        return false;
    }
    shared_resolving_applied_effect(effect)
}

fn resolving_effect_is_only_ability_changes(effect: AppliedEffectDef) -> bool {
    match effect {
        AppliedEffectDef::Composite(effects) => {
            !effects.is_empty()
                && effects
                    .iter()
                    .copied()
                    .all(resolving_effect_is_only_ability_changes)
        }
        AppliedEffectDef::GrantAbility(_) | AppliedEffectDef::RemoveAbilities(_) => true,
        AppliedEffectDef::CannotBeCountered
        | AppliedEffectDef::DoesNotUntapDuringUntapStep
        | AppliedEffectDef::MayChooseNotToUntap
        | AppliedEffectDef::CannotBlock
        | AppliedEffectDef::CannotAttack
        | AppliedEffectDef::CannotBeBlocked
        | AppliedEffectDef::CannotBeEnchanted
        | AppliedEffectDef::CannotBecomeEnchanted
        | AppliedEffectDef::CannotChangeController
        | AppliedEffectDef::RemainsAttachedThroughProtection
        | AppliedEffectDef::CannotBeBlockedBy(_)
        | AppliedEffectDef::CanBlockOnly(_)
        | AppliedEffectDef::PreventDamageFrom(_)
        | AppliedEffectDef::PreventCombatDamageFrom(_)
        | AppliedEffectDef::PreventCombatDamage
        | AppliedEffectDef::PreventCombatDamageDealtBy
        | AppliedEffectDef::AddLandTypes(_)
        | AppliedEffectDef::SetLandTypes(_)
        | AppliedEffectDef::Animate(_)
        | AppliedEffectDef::ModifyPowerToughness { .. }
        | AppliedEffectDef::Special(_) => false,
    }
}

pub(super) fn shared_resolving_applied_effect(effect: AppliedEffectDef) -> bool {
    match effect {
        AppliedEffectDef::Composite(effects) => {
            !effects.is_empty() && effects.iter().copied().all(shared_resolving_applied_effect)
        }
        // These operations are executed directly by the shared apply
        // path; animation reads the whole creature off the definition.
        // "Can't block this turn" joins these: several cards print it as a
        // resolving rider, so the shared apply path has to place it.
        AppliedEffectDef::Animate(_)
        | AppliedEffectDef::ModifyPowerToughness { .. }
        | AppliedEffectDef::CannotBlock
        | AppliedEffectDef::CannotBeBlocked
        | AppliedEffectDef::RemoveAbilities(_) => true,
        AppliedEffectDef::GrantAbility(ability) => match ability.definition {
            DeclarativeAbilityDef::ActivatedMana(definition)
            | DeclarativeAbilityDef::Activated(definition) => {
                battlefield_only(definition.source_zones) && shared_definition_ability(ability)
            }
            DeclarativeAbilityDef::TriggeredMana(_)
            | DeclarativeAbilityDef::Triggered(_)
            | DeclarativeAbilityDef::Replacement(_)
            | DeclarativeAbilityDef::Keyword(_) => shared_definition_ability(ability),
            DeclarativeAbilityDef::AlternativeCast(definition) => {
                definition.kind == AlternativeCastKindDef::Flashback
                    && ability.declarative_effect() == Some(EffectDef::None)
            }
            DeclarativeAbilityDef::Spell(_)
            | DeclarativeAbilityDef::Static(_)
            | DeclarativeAbilityDef::SpecialAction(_)
            | DeclarativeAbilityDef::Legacy => false,
        },
        // The rest are continuous, not an until-end-of-turn rider a spell
        // hands out.
        AppliedEffectDef::CannotAttack
        | AppliedEffectDef::CannotBeCountered
        | AppliedEffectDef::DoesNotUntapDuringUntapStep
        | AppliedEffectDef::MayChooseNotToUntap
        | AppliedEffectDef::CannotBeEnchanted
        | AppliedEffectDef::CannotBecomeEnchanted
        | AppliedEffectDef::CannotChangeController
        | AppliedEffectDef::RemainsAttachedThroughProtection
        | AppliedEffectDef::CannotBeBlockedBy(_)
        | AppliedEffectDef::CanBlockOnly(_)
        | AppliedEffectDef::PreventDamageFrom(_)
        | AppliedEffectDef::PreventCombatDamageFrom(_)
        | AppliedEffectDef::PreventCombatDamage
        | AppliedEffectDef::PreventCombatDamageDealtBy
        | AppliedEffectDef::AddLandTypes(_)
        | AppliedEffectDef::SetLandTypes(_)
        | AppliedEffectDef::Special(_) => false,
    }
}

pub(super) fn shared_activated_costs(source_zones: &[ZoneKind], costs: &[AbilityCostDef]) -> bool {
    let battlefield = source_zones == [ZoneKind::Battlefield];
    let hand = source_zones == [ZoneKind::Hand];
    let graveyard = source_zones == [ZoneKind::Graveyard];
    let sacrifice_choices = costs
        .iter()
        .filter(|cost| matches!(cost, AbilityCostDef::SacrificePermanent { .. }))
        .count();
    let source_exit_costs = costs
        .iter()
        .filter(|cost| {
            matches!(
                cost,
                AbilityCostDef::SacrificeSource | AbilityCostDef::ExileSource
            )
        })
        .count();
    sacrifice_choices <= 1
        && source_exit_costs <= 1
        && costs.iter().all(|cost| match cost {
            // A variable X is offered one activation per affordable
            // value. More than one X in the same cost is not: nothing
            // enumerates a cost that charges X twice.
            AbilityCostDef::Mana(cost) => cost.x_multiplier <= 1,
            // The chosen object comes from the battlefield or from the
            // activating player's own graveyard, so only the predicate
            // needs checking.
            AbilityCostDef::SacrificePermanent { object, .. }
            | AbilityCostDef::TapPermanent { object, .. }
            | AbilityCostDef::ExileCardFromGraveyard(object) => {
                battlefield && shared_object_predicate(*object)
            }
            // Exiling the source is the one cost a card can pay from its own
            // graveyard; the rest of these need a permanent to act on.
            AbilityCostDef::ExileSource => battlefield || graveyard,
            AbilityCostDef::TapSource
            | AbilityCostDef::SacrificeSource
            | AbilityCostDef::RemoveCountersFromSource { .. }
            | AbilityCostDef::PayLife(_)
            | AbilityCostDef::Loyalty(_) => battlefield,
            AbilityCostDef::DiscardSource => hand,
            AbilityCostDef::UntapSource
            | AbilityCostDef::DiscardCards(_)
            | AbilityCostDef::Special(_) => false,
        })
}

pub(super) fn shared_trigger_condition(condition: TriggerConditionDef) -> bool {
    match condition {
        TriggerConditionDef::ObjectCount { query, .. } => shared_object_predicate(query.object),
        TriggerConditionDef::TargetMatches { object, .. }
        | TriggerConditionDef::AttachedPermanentMatches { object } => {
            shared_object_predicate(object)
        }
        TriggerConditionDef::SourceOnBattlefield
        | TriggerConditionDef::SourceUntapped
        | TriggerConditionDef::ActivePlayer(_)
        | TriggerConditionDef::SourceLoyalty { .. }
        | TriggerConditionDef::SourceCounters { .. }
        | TriggerConditionDef::ControlsGreatestPowerCreature
        | TriggerConditionDef::SourceActivationsThisTurn { .. }
        | TriggerConditionDef::SourceDealtDamageToOpponentThisTurn
        | TriggerConditionDef::SourceIsTapped
        | TriggerConditionDef::SpellsCastLastTurn { .. } => true,
    }
}

/// Static effects have a battlefield source but no captured trigger event,
/// resolving ability, or stack-target scope. Keep their condition boundary to
/// the source-state predicates that can be evaluated from exactly that input.
fn shared_static_trigger_condition(condition: TriggerConditionDef) -> bool {
    matches!(
        condition,
        // Counters live on the source, so a static clause can read them from
        // exactly the input it has.
        TriggerConditionDef::SourceOnBattlefield
            | TriggerConditionDef::SourceUntapped
            | TriggerConditionDef::SourceCounters { .. }
            // Reachable from the source by following its attachment, which
            // is exactly the input a static clause has.
            | TriggerConditionDef::AttachedPermanentMatches { .. }
    )
}

/// Only one object is chosen, and only from the two places the casting
/// enumeration looks: the caster's own battlefield and graveyard.
fn shared_spell_additional_cost(cost: Option<SpellAdditionalCostDef>) -> bool {
    let Some(cost) = cost else {
        return true;
    };
    cost.count == 1
        && matches!(
            cost.zone,
            ZoneKind::Battlefield | ZoneKind::Graveyard | ZoneKind::Hand
        )
        && shared_object_predicate(cost.object)
}

pub(super) fn battlefield_only(zones: &[ZoneKind]) -> bool {
    zones == [ZoneKind::Battlefield]
}

#[allow(clippy::too_many_lines)]
pub(super) fn shared_definition_ability(ability: &AbilityDef) -> bool {
    let Some(effect) = ability.declarative_effect() else {
        return false;
    };
    match ability.definition {
        DeclarativeAbilityDef::Spell(definition) => {
            if let Some(modal) = definition.modal() {
                modal.modes.iter().all(|mode| {
                    mode.declarative_effect().is_none() || shared_definition_ability(mode)
                })
            } else {
                // A clause that exists only to carry an additional cost has
                // nothing to do on resolution, which is why None is allowed
                // here and nowhere else.
                let cost = definition.additional_cost();
                shared_spell_additional_cost(cost)
                    && (shared_stack_effect(effect)
                        || (cost.is_some() && effect == EffectDef::None))
            }
        }
        DeclarativeAbilityDef::ActivatedMana(definition) => {
            fn spends_its_source(definition: ActivatedAbilityDef) -> bool {
                definition.costs.iter().any(|cost| {
                    matches!(
                        cost,
                        AbilityCostDef::TapSource
                            | AbilityCostDef::SacrificeSource
                            | AbilityCostDef::ExileSource
                    )
                })
            }

            battlefield_only(definition.source_zones)
                && definition.procedure == AbilityProcedureDef::Shared
                && !definition.costs.as_slice().is_empty()
                && definition.costs.iter().all(|cost| {
                    matches!(
                        cost,
                        AbilityCostDef::TapSource
                            | AbilityCostDef::SacrificeSource
                            | AbilityCostDef::ExileSource
                            | AbilityCostDef::RemoveCountersFromSource { .. }
                            | AbilityCostDef::PayLife(_)
                    ) || matches!(
                        cost,
                        // Mana is paid out of the pool, so the ability also
                        // has to spend its source; hybrid and {X} would need
                        // a choice the activation cannot carry.
                        AbilityCostDef::Mana(mana)
                            if !mana.variable_x
                                && mana.hybrid.iter().all(|count| *count == 0)
                                && spends_its_source(definition)
                    )
                })
                && definition
                    .costs
                    .iter()
                    .filter(|cost| {
                        matches!(
                            cost,
                            AbilityCostDef::SacrificeSource | AbilityCostDef::ExileSource
                        )
                    })
                    .count()
                    <= 1
                && shared_mana_effect(effect, true)
        }
        DeclarativeAbilityDef::TriggeredMana(definition) => {
            fn immediate_mana_effect(effect: EffectDef) -> bool {
                match effect {
                    EffectDef::Sequence(effects) => {
                        !effects.is_empty() && effects.iter().copied().all(immediate_mana_effect)
                    }
                    EffectDef::AddMana(_) => shared_mana_effect(effect, false),
                    // A mana ability's amount has to be knowable without
                    // resolving it, which this one is not.
                    EffectDef::AddManaEqualTo { .. }
                    | EffectDef::Randomized { .. }
                    | EffectDef::ChoosePermanent { .. }
                    | EffectDef::ChooseDamageSource { .. }
                    | EffectDef::PreventNextDamageFromSource { .. }
                    | EffectDef::May { .. }
                    | EffectDef::None
                    | EffectDef::DealDamage { .. }
                    | EffectDef::DrainLife { .. }
                    | EffectDef::GainLife { .. }
                    | EffectDef::AddPoisonCounters { .. }
                    | EffectDef::DrawCards { .. }
                    | EffectDef::Discard { .. }
                    | EffectDef::ShuffleLibrary { .. }
                    | EffectDef::EmptyManaPool { .. }
                    | EffectDef::LoseLife { .. }
                    | EffectDef::LoseTheGame { .. }
                    | EffectDef::Regenerate { .. }
                    | EffectDef::Tap { .. }
                    | EffectDef::RemoveFromCombat { .. }
                    | EffectDef::SetColor { .. }
                    | EffectDef::DestroyAtEndOfCombat { .. }
                    | EffectDef::SkipNextUntapSteps { .. }
                    | EffectDef::RemoveAllCounters { .. }
                    | EffectDef::Untap { .. }
                    | EffectDef::PreventAllCombatDamageThisTurn
                    | EffectDef::PreventNextDamage { .. }
                    | EffectDef::PreventAllDamageThisTurn { .. }
                    | EffectDef::PreventCombatDamageThisTurn { .. }
                    | EffectDef::PreventCombatDamageDealtByThisTurn { .. }
                    | EffectDef::PreventDamageDealtByThisTurn { .. }
                    | EffectDef::PreventDamageToPlayerAndControlledCreaturesThisTurn { .. }
                    | EffectDef::PreventDamageToPlayerFromThisTurn { .. }
                    | EffectDef::PreventAllCombatDamageExceptSourceThisTurn { .. }
                    | EffectDef::Attach { .. }
                    | EffectDef::CreateToken { .. }
                    | EffectDef::CreateTokenCopyOf { .. }
                    | EffectDef::Destroy { .. }
                    | EffectDef::Sacrifice { .. }
                    | EffectDef::SacrificeOfChoice { .. }
                    | EffectDef::DestroyOfChoice { .. }
                    | EffectDef::SplitPermanentsAndSacrificeAPile { .. }
                    | EffectDef::RevealAndSplitIntoPiles { .. }
                    | EffectDef::Mill { .. }
                    | EffectDef::LookAtTopAndMayTake { .. }
                    | EffectDef::LookAtTopAndSelect { .. }
                    | EffectDef::LookAtHand { .. }
                    | EffectDef::SearchZone { .. }
                    | EffectDef::ChooseCards { .. }
                    | EffectDef::ReplaceNextDrawThisTurn { .. }
                    | EffectDef::IfFormat { .. }
                    | EffectDef::Counter { .. }
                    | EffectDef::CounterUnlessPaid { .. }
                    | EffectDef::AddCounters { .. }
                    | EffectDef::ChangeTextBasicLandType { .. }
                    | EffectDef::BecomeCopyOf { .. }
                    | EffectDef::OptionalPayment { .. }
                    | EffectDef::UnlessPaid { .. }
                    | EffectDef::CannotBeForcedToSacrifice
                    | EffectDef::CreateEmblem { .. }
                    | EffectDef::Transform { .. }
                    | EffectDef::AdditionalCombatPhase
                    | EffectDef::TakeExtraTurn { .. }
                    | EffectDef::CannotCastNoncreatureSpellsThisTurn { .. }
                    | EffectDef::GrantFlashToNextSorcery
                    | EffectDef::ExileLinkedToSource { .. }
                    | EffectDef::ReturnLinkedExiles { .. }
                    | EffectDef::Detain { .. }
                    | EffectDef::CannotRegenerateThisTurn { .. }
                    | EffectDef::MakeUnblockableThisTurn { .. }
                    | EffectDef::GainControlWhileSourceRemains { .. }
                    | EffectDef::GainControlThisTurn { .. }
                    | EffectDef::AtNextStep { .. }
                    | EffectDef::IfCondition { .. }
                    | EffectDef::TriggerUntilYourNextTurn { .. }
                    | EffectDef::ReduceGenericCostBy(_)
                    | EffectDef::PlayersCantPlay(_)
                    | EffectDef::LandwalkCanBeBlocked(_)
                    | EffectDef::CannotAttackUnless(_)
                    | EffectDef::MultiplyEventAmount(_)
                    | EffectDef::Replacement(_)
                    | EffectDef::MoveToZone { .. }
                    | EffectDef::ChooseCardName { .. }
                    | EffectDef::ChoosePlayer { .. }
                    | EffectDef::CopyPermanentAsItEnters { .. }
                    | EffectDef::ChooseCreatureType { .. }
                    | EffectDef::Apply { .. }
                    | EffectDef::Special(_) => false,
                }
            }
            definition.condition.is_none()
                && definition.procedure == AbilityProcedureDef::Shared
                && battlefield_only(definition.source_zones)
                && shared_trigger_event(definition.event)
                && immediate_mana_effect(effect)
        }
        DeclarativeAbilityDef::Activated(definition) => {
            matches!(
                    definition.source_zones,
                    [ZoneKind::Battlefield | ZoneKind::Hand | ZoneKind::Graveyard]
                ) && definition.procedure == AbilityProcedureDef::Shared
                    && shared_activated_costs(definition.source_zones, definition.costs.as_slice())
                    // An activation enumerates its targets once for every
                    // affordable X, so a slot dividing X has no enumeration
                    // to live in yet.
                    && definition
                        .targets
                        .iter()
                        .all(|slot| slot.divided_total.is_none())
                    && shared_stack_effect(effect)
        }
        DeclarativeAbilityDef::Triggered(definition) => {
            // A state trigger is nothing but its condition: without one it
            // would trigger on every state-based check forever.
            let condition_is_required = definition.event != TriggerEventDef::StateCondition
                || definition.condition.is_some();
            battlefield_only(definition.source_zones)
                && definition.procedure == AbilityProcedureDef::Shared
                && shared_trigger_event(definition.event)
                && condition_is_required
                && definition
                    .condition
                    .is_none_or(|condition| shared_trigger_condition(*condition))
                && shared_stack_effect(effect)
        }
        DeclarativeAbilityDef::Static(definition) => {
            (effect == EffectDef::None
                && ability.coverage.status == ImplementationStatus::Complete
                && ability.coverage.explanation.is_some())
                || shared_static_effect(definition.source_zones, effect)
        }
        DeclarativeAbilityDef::Replacement(definition) => match definition.event {
            // A permanent's own entry replacement may be optional: the entry
            // path asks its controller. Only its own -- an optional
            // replacement watching another object is still unsupported.
            // Only a permanent's own entry replacement reads a condition.
            // The two below watch another object and have no source to ask.
            ReplacementEventDef::SourceEntersBattlefield => {
                battlefield_only(definition.source_zones)
                    && shared_replacement_event(definition.event)
                    && matches!(effect, EffectDef::Replacement(effect) if shared_entry_replacement_effect(effect))
            }
            ReplacementEventDef::ObjectEntersBattlefield { .. } => {
                !definition.optional
                    && definition.condition.is_none()
                    && battlefield_only(definition.source_zones)
                    && shared_replacement_event(definition.event)
                    && matches!(effect, EffectDef::Replacement(effect) if shared_entry_replacement_effect(effect))
            }
            ReplacementEventDef::EntersBattlefield => {
                !definition.optional
                    && definition.condition.is_none()
                    && battlefield_only(definition.source_zones)
                    && matches!(
                        effect,
                        EffectDef::ChooseCreatureType {
                            object: EffectRecipientDef::Source,
                        } | EffectDef::ChooseCardName {
                            object: EffectRecipientDef::Source,
                        } | EffectDef::ChoosePlayer {
                            object: EffectRecipientDef::Source,
                            ..
                        } | EffectDef::CopyPermanentAsItEnters { .. }
                    )
            }
            ReplacementEventDef::WouldMove { from, to, cause } => {
                !definition.optional
                    && definition.condition.is_none()
                    && definition.source_zones == [from]
                    && ((from == ZoneKind::Hand
                        && to == ZoneKind::Graveyard
                        && shared_zone_move_cause(cause)
                        && effect
                            == EffectDef::MoveToZone {
                                object: EffectRecipientDef::Source,
                                zone: ZoneKind::Battlefield,
                                controller: None,
                                placement: ZonePlacement::Top,
                            })
                        || (from == ZoneKind::Battlefield
                            && to == ZoneKind::Graveyard
                            && cause == ZoneMoveCauseDef::Any
                            && matches!(
                                effect,
                                EffectDef::Replacement(effect)
                                    if shared_battlefield_exit_replacement_effect(effect)
                            )))
            }
            ReplacementEventDef::AnyObjectWouldMove { .. } => {
                !definition.optional
                    && definition.condition.is_none()
                    && battlefield_only(definition.source_zones)
                    && shared_replacement_event(definition.event)
                    && effect
                        == EffectDef::MoveToZone {
                            object: EffectRecipientDef::Source,
                            zone: ZoneKind::Exile,
                            controller: None,
                            placement: ZonePlacement::Top,
                        }
            }
            ReplacementEventDef::WouldGainLife(_) => {
                !definition.optional
                    && definition.condition.is_none()
                    && battlefield_only(definition.source_zones)
                    && matches!(effect, EffectDef::MultiplyEventAmount(_))
            }
            ReplacementEventDef::WouldBeginTurn { .. } => {
                definition
                    .condition
                    .is_none_or(|condition| condition == ReplacementConditionDef::SourceTapped)
                    && battlefield_only(definition.source_zones)
                    && matches!(
                        effect,
                        EffectDef::Replacement(effect)
                            if shared_begin_turn_replacement_effect(effect)
                    )
            }
            ReplacementEventDef::Special(_) => false,
        },
        DeclarativeAbilityDef::AlternativeCast(definition) => match definition.kind {
            // Both are permission to cast rather than effects of their
            // own; the card's spell clause does the work.
            AlternativeCastKindDef::Flashback | AlternativeCastKindDef::Miracle => {
                effect == EffectDef::None
            }
            AlternativeCastKindDef::Overload => shared_stack_effect(effect),
        },
        DeclarativeAbilityDef::Keyword(keyword) => shared_keyword(keyword),
        DeclarativeAbilityDef::SpecialAction(_) | DeclarativeAbilityDef::Legacy => false,
    }
}
