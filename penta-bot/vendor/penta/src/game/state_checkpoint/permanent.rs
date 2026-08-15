#![allow(clippy::wildcard_imports)]

use super::*;

#[allow(clippy::too_many_lines)]
pub(super) fn permanent_snapshot(
    catalog: &CardCatalog,
    permanent: &Permanent,
) -> PermanentSnapshot {
    let temporary_granted_abilities = permanent
        .temporary_granted_abilities
        .iter()
        .filter_map(|grant| {
            Some(TemporaryGrantedAbilitySnapshot {
                ability: ability_locator(catalog, |ability| *ability == grant.ability)?,
                source: grant.source.0,
                source_definition: grant.source_definition.0,
                source_part_id: grant.source_part.0,
                source_ability_id: grant.source_ability.0,
                grant_id: grant.grant.0,
                timestamp: grant.timestamp.0,
                order: grant.order,
                expiration: expiration_snapshot(grant.expiration),
            })
        })
        .collect::<Vec<_>>();
    let has_unlocated_grant =
        temporary_granted_abilities.len() != permanent.temporary_granted_abilities.len();
    let temporary_removed_abilities = permanent
        .temporary_removed_abilities
        .iter()
        .filter_map(|removal| {
            Some(TemporaryRemovedAbilitySnapshot {
                effect: applied_effect_locator(
                    catalog,
                    AppliedEffectDef::RemoveAbilities(removal.predicate),
                )?,
                timestamp: removal.timestamp.0,
                order: removal.order,
                expiration: expiration_snapshot(removal.expiration),
            })
        })
        .collect::<Vec<_>>();
    let has_unlocated_removal =
        temporary_removed_abilities.len() != permanent.temporary_removed_abilities.len();
    let copy_effect = permanent.copy_effect.as_ref().map(|copy| {
        let added_abilities = copy
            .added_abilities
            .iter()
            .filter_map(|ability| {
                Some(CopiableAbilitySnapshot {
                    origin: ability_origin_snapshot(ability.origin),
                    ability: ability_locator(catalog, |candidate| {
                        *candidate == ability.definition
                    })?,
                })
            })
            .collect::<Vec<_>>();
        let complete = added_abilities.len() == copy.added_abilities.len();
        (
            CopiableCharacteristicsSnapshot {
                definition: copy.base.0.0,
                part_id: copy.base.1.0,
                added_types: CardType::ALL.map(|card_type| copy.added_types.contains(card_type)),
                added_abilities,
            },
            complete,
        )
    });
    let has_unlocated_copy_ability = copy_effect.as_ref().is_some_and(|(_, complete)| !complete);
    PermanentSnapshot {
        object_id: permanent.card.id.0,
        owner: permanent.card.owner.index(),
        timestamp: permanent.timestamp.0,
        entered_controller_turn: permanent.entered_controller_turn,
        power_bonus: permanent.power_bonus,
        toughness_bonus: permanent.toughness_bonus,
        while_source_tapped: permanent
            .while_source_tapped
            .iter()
            .map(|bonus| (bonus.source.0, bonus.power, bonus.toughness))
            .collect(),
        unblockable_this_turn: permanent.unblockable_this_turn,
        cannot_block_this_turn: permanent.cannot_block_this_turn,
        detained_until_turn_of: permanent
            .detained_until_turn_of
            .map(|(player, turns)| (player.index(), turns)),
        destroy_at_end_of_combat: permanent.destroy_at_end_of_combat,
        skipped_untap_steps: permanent.skipped_untap_steps,
        color_override: permanent.color_override.map(ColorSet::to_flags),
        combat_damage_prevented: permanent.combat_damage_prevented,
        combat_damage_dealt_by_prevented: permanent.combat_damage_dealt_by_prevented,
        damage_dealt_by_prevented: permanent.damage_dealt_by_prevented,
        control_reverts_to: permanent.control_reverts_to.map(PlayerId::index),
        cannot_regenerate_this_turn: permanent.cannot_regenerate_this_turn,
        control_source: permanent.control_source.map(|id| id.0),
        control_requires_source_tapped: permanent.control_requires_source_tapped,
        chosen_player: permanent.chosen_player.map(PlayerId::index),
        destroy_at_end: permanent.destroy_at_end,
        counters: permanent.counters.to_vec(),
        attached_to: permanent.attached_to.map(|id| id.0),
        exile_instead_of_dying: permanent.exile_instead_of_dying,
        combat_damage_assignment: permanent
            .combat_damage_assignment
            .iter()
            .map(|assignment| CombatDamageAssignmentSnapshot {
                recipient: target_snapshot(assignment.recipient),
                amount: assignment.amount,
            })
            .collect(),
        regeneration_shields: permanent.regeneration_shields,
        attacked_this_turn: permanent.attacked_this_turn,
        attacks_this_turn: permanent.attacks_this_turn,
        damage_sources: permanent.damage_sources.iter().map(|id| id.0).collect(),
        dealt_damage_to_opponent_this_turn: permanent.dealt_damage_to_opponent_this_turn,
        deathtouch_damage: permanent.deathtouch_damage,
        created_by: permanent.created_by.map(|id| id.0),
        animation: permanent.animation.map(animation_snapshot),
        temporary_keywords: permanent
            .temporary_keywords
            .iter()
            .copied()
            .map(keyword_snapshot)
            .collect(),
        keywords_until_upkeep_of: permanent
            .keywords_until_upkeep_of
            .iter()
            .map(|(player, keyword)| UpkeepKeywordSnapshot {
                seat: player.index(),
                keyword: keyword_snapshot(*keyword),
            })
            .collect(),
        temporary_granted_abilities,
        temporary_removed_abilities,
        activations_this_turn: permanent
            .activations_this_turn
            .iter()
            .map(|(origin, count)| AbilityActivationSnapshot {
                origin: ability_origin_snapshot(*origin),
                count: *count,
            })
            .collect(),
        copy_effect: copy_effect.map(|(snapshot, _)| snapshot),
        copied_from: permanent
            .copied_from
            .map(|(definition, part)| CopiedFromSnapshot {
                definition: definition.0,
                part_id: part.0,
            }),
        text_changes: permanent
            .text_changes
            .iter()
            .map(|change| model::BasicLandTypeChangeSnapshot {
                from: basic_land_type_snapshot(change.from),
                to: basic_land_type_snapshot(change.to),
            })
            .collect(),
        has_dynamic_characteristics: has_unlocated_grant
            || has_unlocated_removal
            || has_unlocated_copy_ability,
    }
}

pub(super) fn detached_permanent_snapshot(
    catalog: &CardCatalog,
    permanent: &Permanent,
) -> DetachedPermanentSnapshot {
    DetachedPermanentSnapshot {
        state: permanent_snapshot(catalog, permanent),
        definition: permanent.card.definition.0,
        presented_part_id: permanent.presented.0,
        controller: permanent.controller.index(),
        tapped: permanent.tapped,
        damage: permanent.damage,
        attacking: permanent.attacking,
        attack_defender: permanent.attack_defender.map(|defender| match defender {
            AttackDefender::Player(player) => AttackDefenderSnapshot::Player {
                seat: player.index(),
            },
            AttackDefender::Planeswalker(object) => AttackDefenderSnapshot::Planeswalker {
                object_id: object.0,
            },
        }),
        blocked: permanent.blocked,
        blocking: permanent.blocking.map(|id| id.0),
        activated_loyalty_this_turn: permanent.activated_loyalty_this_turn,
        chosen_creature_type: permanent.chosen_creature_type.clone(),
        chosen_card_name: permanent.chosen_card_name.clone(),
    }
}
