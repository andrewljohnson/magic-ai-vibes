//! Dealing and publishing combat damage.
//!
//! Split out of the parent module for the source-size budget.

#![allow(clippy::wildcard_imports)]

use super::*;

impl Game {
    /// How much life a drain can take from a recipient: what it had before
    /// the damage, which is all it can give however much is dealt.
    pub(in crate::game) fn drainable_from(&self, target: Target) -> u16 {
        match target {
            Target::Player(player) => self.players[player.index()].life.max(0).cast_unsigned(),
            Target::Permanent(id) => self
                .battlefield
                .iter()
                .find(|permanent| permanent.card.id == id)
                .and_then(|permanent| {
                    if self
                        .permanent_types(permanent)
                        .is_some_and(|types| types.contains(CardType::Planeswalker))
                    {
                        return Some(permanent.counters(CounterKind::Loyalty));
                    }
                    self.toughness(permanent)
                        .map(|value| value.max(0).cast_unsigned())
                })
                .unwrap_or(0),
            Target::Card(_) | Target::Spell(_) => 0,
        }
    }

    /// Raises the event for damage a player took, whatever dealt it. Only a
    /// battlefield source can be recognised, which is what every trigger that
    /// reads this needs.
    pub(in crate::game) fn publish_damage_to_player(
        &mut self,
        source: Option<GameObjectId>,
        player: PlayerId,
        amount: u16,
    ) {
        if amount == 0 {
            return;
        }
        let Some(source) = source.and_then(|source| {
            self.battlefield
                .iter()
                .find(|permanent| permanent.card.id == source)
        }) else {
            return;
        };
        let event = CommittedTriggerEvent::DamageDealtToPlayer {
            object: self.trigger_event_object(source),
            player,
            amount,
        };
        self.capture_battlefield_triggers(&event);
    }

    /// Combat damage from an attacker to a player, which is the one kind of
    /// damage the "whenever this deals combat damage to a player" triggers
    /// listen for. Ordinary damage to a player carries no such event.
    /// Combat damage from an attacker to whatever it is attacking. A player
    /// also gets the "deals combat damage to a player" event; a planeswalker
    /// takes the damage as a permanent, which its loyalty counters absorb.
    pub(in crate::game) fn deal_combat_damage_to(
        &mut self,
        attacker: GameObjectId,
        defender: Target,
        amount: u16,
    ) {
        match defender {
            Target::Player(player) => self.deal_combat_damage_to_player(attacker, player, amount),
            Target::Permanent(_) | Target::Card(_) | Target::Spell(_) => {
                // Flagged as combat damage so a trigger that listens for it
                // arriving here, as Vraska's does, can tell it apart from an
                // ability's damage.
                self.damage_target_from_kind(Some(attacker), Some(defender), amount, true);
            }
        }
    }

    pub(in crate::game) fn deal_combat_damage_to_player(
        &mut self,
        attacker: GameObjectId,
        player: PlayerId,
        amount: u16,
    ) {
        let dealt = self.damage_target_from_kind(
            Some(attacker),
            Some(Target::Player(player)),
            amount,
            true,
        );
        if dealt == 0 {
            return;
        }
        let Some(source) = self
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == attacker)
        else {
            return;
        };
        let event = CommittedTriggerEvent::CombatDamageDealtToPlayer {
            object: self.trigger_event_object(source),
            player,
            amount: dealt,
        };
        self.capture_battlefield_triggers(&event);
    }

    /// Combat damage between one blocked attacker and everything blocking it,
    /// in both directions.
    pub(in crate::game) fn exchange_blocked_combat_damage(
        &mut self,
        attacker_id: GameObjectId,
        attacker_index: usize,
        blockers: &[GameObjectId],
        attacker_deals_damage: bool,
    ) {
        let assignments = self.battlefield[attacker_index]
            .combat_damage_assignment
            .clone();
        if attacker_deals_damage {
            let split = if assignments.is_empty() {
                self.default_damage_split(attacker_id, blockers)
            } else {
                assignments
                    .into_iter()
                    .map(|assignment| (assignment.recipient, assignment.amount))
                    .collect()
            };
            for (recipient, amount) in split {
                if self.combat_damage_is_prevented_for(recipient) {
                    continue;
                }
                // Trample past a blocker is still combat damage to a player,
                // so it goes through the same path as an unblocked hit.
                if let Target::Player(player) = recipient {
                    self.deal_combat_damage_to_player(attacker_id, player, amount);
                } else {
                    self.damage_target_from_kind(Some(attacker_id), Some(recipient), amount, true);
                }
            }
        }
        if self.combat_damage_is_prevented_for(Target::Permanent(attacker_id)) {
            return;
        }
        let return_damage = blockers
            .iter()
            .filter_map(|id| {
                self.battlefield
                    .iter()
                    .find(|permanent| permanent.card.id == *id)
                    .filter(|permanent| {
                        self.deals_damage_in_current_combat_step(permanent)
                            && !self.combat_damage_is_prevented_from(permanent.card.id)
                    })
                    .and_then(|permanent| self.power(permanent))
                    .map(|power| (*id, power.max(0).cast_unsigned()))
            })
            .collect::<Vec<_>>();
        for (blocker, amount) in return_damage {
            self.damage_target_from_kind(
                Some(blocker),
                Some(Target::Permanent(attacker_id)),
                amount,
                true,
            );
        }
    }

    pub(in crate::game) fn combat_defender(attacker: &Permanent) -> AttackDefender {
        attacker
            .attack_defender
            .unwrap_or(AttackDefender::Player(attacker.controller.opponent()))
    }

    pub(in crate::game) fn combat_defender_target(&self, attacker: &Permanent) -> Option<Target> {
        match Self::combat_defender(attacker) {
            AttackDefender::Player(player) => Some(Target::Player(player)),
            AttackDefender::Planeswalker(id) => self
                .battlefield
                .iter()
                .find(|permanent| {
                    permanent.card.id == id
                        && permanent.controller != attacker.controller
                        && self
                            .permanent_types(permanent)
                            .is_some_and(|types| types.contains(CardType::Planeswalker))
                })
                .map(|permanent| Target::Permanent(permanent.card.id)),
        }
    }
}
