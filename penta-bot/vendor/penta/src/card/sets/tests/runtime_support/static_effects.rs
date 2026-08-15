//! Which static abilities the shared runtime can execute.
//!
//! A static effect is read live off the battlefield rather than resolved, so
//! the questions here are different from the stack's: what may be applied
//! continuously, to whom, and for how long.

use super::*;

/// The two static effects that are not an `Apply`: a prohibition read off
/// the battlefield, and a cost reduction read out of hand.
pub(in super::super) fn shared_static_non_apply_effect(
    source_zones: &[ZoneKind],
    effect: EffectDef,
) -> bool {
    match effect {
        // Both are read off the battlefield and neither carries anything
        // further to check: one names a land type, the other nothing at all.
        EffectDef::CannotBeForcedToSacrifice | EffectDef::LandwalkCanBeBlocked(_) => {
            battlefield_only(source_zones)
        }
        // Read while attackers are declared, over the battlefield, so only
        // the object predicate is left to check.
        EffectDef::CannotAttackUnless(query) => {
            battlefield_only(source_zones)
                && query.zones == [ZoneKind::Battlefield]
                && shared_object_predicate(query.object)
        }
        // The prohibition is read off the battlefield while play options
        // are offered, and only against a card's printed shape.
        EffectDef::PlayersCantPlay(predicate) => {
            battlefield_only(source_zones) && shared_object_predicate(*predicate)
        }
        EffectDef::ReduceGenericCostBy(value) => {
            source_zones == [ZoneKind::Hand]
                && matches!(
                    value,
                    crate::card::ValueDef::Constant(_)
                        | crate::card::ValueDef::CountMatchingObjects(_)
                )
        }
        EffectDef::Sequence(effects) => {
            !effects.is_empty()
                && effects
                    .iter()
                    .copied()
                    .all(|effect| shared_static_effect(source_zones, effect))
        }
        _ => false,
    }
}

#[allow(clippy::too_many_lines)]
pub(in super::super) fn shared_static_effect(source_zones: &[ZoneKind], effect: EffectDef) -> bool {
    match effect {
        EffectDef::CannotBeForcedToSacrifice
        | EffectDef::ReduceGenericCostBy(_)
        | EffectDef::PlayersCantPlay(_)
        | EffectDef::LandwalkCanBeBlocked(_)
        | EffectDef::CannotAttackUnless(_)
        | EffectDef::Sequence(_) => shared_static_non_apply_effect(source_zones, effect),
        EffectDef::Apply {
            recipient,
            effect,
            duration,
        } => {
            let battlefield_recipient_is_supported = match recipient {
                EffectRecipientDef::Source | EffectRecipientDef::AttachedPermanent => true,
                EffectRecipientDef::MatchingObjects { object, zones, .. } => {
                    zones == [ZoneKind::Battlefield] && shared_object_predicate(object)
                }
                EffectRecipientDef::Controller
                | EffectRecipientDef::Opponent
                | EffectRecipientDef::EachPlayer
                | EffectRecipientDef::ChosenPermanent(_)
                | EffectRecipientDef::Target(_)
                | EffectRecipientDef::ControllerOfTarget(_)
                | EffectRecipientDef::ObjectsControlledByTarget { .. }
                | EffectRecipientDef::ObjectsOwnedByTarget { .. }
                | EffectRecipientDef::CardsOwnedByTarget { .. }
                | EffectRecipientDef::ObjectsSharingNameWithTarget(_)
                | EffectRecipientDef::TriggeringObject
                | EffectRecipientDef::ControllerOfTriggeringObject
                | EffectRecipientDef::ControllerOfAttachedPermanent
                | EffectRecipientDef::EventPlayer => false,
            };
            let battlefield_effect_is_supported = shared_static_applied_effect(recipient, effect);
            let battlefield_effect = battlefield_only(source_zones)
                && battlefield_recipient_is_supported
                && battlefield_effect_is_supported
                && matches!(
                    duration,
                    EffectDurationDef::WhileSourceRemainsInZone
                        | EffectDurationDef::UntilSourceLeavesZone
                );
            let stack_source_effect = source_zones == [ZoneKind::Stack]
                && recipient == EffectRecipientDef::Source
                && shared_cannot_be_countered_effect(effect)
                && duration == EffectDurationDef::WhileSourceRemainsInZone;
            battlefield_effect || stack_source_effect
        }
        EffectDef::IfCondition { condition, then } => {
            battlefield_only(source_zones)
                && shared_static_trigger_condition(*condition)
                && shared_static_effect(source_zones, *then)
        }
        // None of these is a static ability; all execute from the stack.
        EffectDef::GrantFlashToNextSorcery
        | EffectDef::Randomized { .. }
        | EffectDef::ChoosePermanent { .. }
        | EffectDef::ChooseDamageSource { .. }
        | EffectDef::PreventNextDamageFromSource { .. }
        | EffectDef::May { .. }
        | EffectDef::ExileLinkedToSource { .. }
        | EffectDef::ReturnLinkedExiles { .. }
        | EffectDef::Detain { .. }
        | EffectDef::CannotRegenerateThisTurn { .. }
        | EffectDef::MakeUnblockableThisTurn { .. }
        | EffectDef::GainControlWhileSourceRemains { .. }
        | EffectDef::GainControlThisTurn { .. }
        | EffectDef::AtNextStep { .. }
        | EffectDef::TriggerUntilYourNextTurn { .. }
        | EffectDef::None
        | EffectDef::AddMana(_)
        | EffectDef::AddManaEqualTo { .. }
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
        | EffectDef::MultiplyEventAmount(_)
        | EffectDef::Replacement(_)
        | EffectDef::MoveToZone { .. }
        | EffectDef::ChooseCardName { .. }
        | EffectDef::ChoosePlayer { .. }
        | EffectDef::CopyPermanentAsItEnters { .. }
        | EffectDef::ChooseCreatureType { .. }
        | EffectDef::CreateEmblem { .. }
        | EffectDef::Transform { .. }
        | EffectDef::AdditionalCombatPhase
        | EffectDef::TakeExtraTurn { .. }
        | EffectDef::CannotCastNoncreatureSpellsThisTurn { .. }
        | EffectDef::Special(_) => false,
    }
}

pub(in super::super) fn shared_static_applied_effect(
    recipient: EffectRecipientDef,
    effect: AppliedEffectDef,
) -> bool {
    match effect {
        AppliedEffectDef::Composite(effects) => {
            !effects.is_empty()
                && effects
                    .iter()
                    .copied()
                    .all(|effect| shared_static_applied_effect(recipient, effect))
        }
        AppliedEffectDef::ModifyPowerToughness { power, toughness } => {
            static_stat_value(power) && static_stat_value(toughness)
        }
        AppliedEffectDef::AddLandTypes(land_types) | AppliedEffectDef::SetLandTypes(land_types) => {
            !land_types.is_empty()
        }
        AppliedEffectDef::GrantAbility(ability) => shared_definition_ability(ability),
        // A blocking restriction is read off the ordinary static-effect walk
        // over the attacker, so a group recipient works exactly as a
        // self-applied one does: Bower Passage names every creature you
        // control rather than only itself. The other two keep the narrower
        // list because no card applies them to a group.
        AppliedEffectDef::CannotBeBlockedBy(predicate) => {
            matches!(
                recipient,
                EffectRecipientDef::Source
                    | EffectRecipientDef::AttachedPermanent
                    | EffectRecipientDef::MatchingObjects { .. }
            ) && shared_object_predicate(predicate)
        }
        AppliedEffectDef::CanBlockOnly(predicate)
        | AppliedEffectDef::PreventDamageFrom(predicate)
        | AppliedEffectDef::PreventCombatDamageFrom(predicate) => {
            matches!(
                recipient,
                EffectRecipientDef::Source | EffectRecipientDef::AttachedPermanent
            ) && shared_object_predicate(predicate)
        }
        // A static combat-damage prevention carries no predicate, so only the
        // recipient it is applied to has to be one the runtime understands.
        AppliedEffectDef::PreventCombatDamage | AppliedEffectDef::PreventCombatDamageDealtBy => {
            matches!(
                recipient,
                EffectRecipientDef::Source | EffectRecipientDef::AttachedPermanent
            )
        }
        // Read only off the Aura whose attachment it is defending, which is
        // the source of the ability granting the protection.
        AppliedEffectDef::RemainsAttachedThroughProtection => {
            recipient == EffectRecipientDef::Source
        }
        AppliedEffectDef::DoesNotUntapDuringUntapStep
        | AppliedEffectDef::MayChooseNotToUntap
        | AppliedEffectDef::CannotBlock
        | AppliedEffectDef::CannotAttack
        | AppliedEffectDef::CannotBeBlocked
        | AppliedEffectDef::RemoveAbilities(_)
        | AppliedEffectDef::CannotBeCountered
        | AppliedEffectDef::CannotBeEnchanted
        | AppliedEffectDef::CannotBecomeEnchanted
        | AppliedEffectDef::CannotChangeController => true,
        // A static animation is read live rather than materialised, so it
        // is held to what `Game::static_animation` can stratify: it may only
        // add the creature type and stats, and it may only be aimed by
        // predicates that cannot read what it supplies.
        AppliedEffectDef::Animate(animation) => {
            Game::static_animation_is_additive(animation)
                && match recipient {
                    EffectRecipientDef::MatchingObjects { object, zones, .. } => {
                        zones == [ZoneKind::Battlefield]
                            && Game::static_animation_predicate_is_supported(object)
                    }
                    _ => false,
                }
        }
        AppliedEffectDef::Special(_) => false,
    }
}

/// The values a static power/toughness bonus may be built from. They are the
/// ones the layer walk can evaluate without reading a resolving spell, and a
/// scale is allowed only over another such value.
fn static_stat_value(value: crate::card::ValueDef) -> bool {
    match value {
        crate::card::ValueDef::Constant(_)
        | crate::card::ValueDef::AnyMatchingObject(_)
        | crate::card::ValueDef::CountMatchingObjects(_) => true,
        crate::card::ValueDef::Scaled(scaled) => static_stat_value(scaled.value),
        _ => false,
    }
}
