//! Exert (CR 701.38).
//!
//! A choice made as a creature is declared as an attacker, and paid for
//! afterwards: an exerted creature is skipped by its controller's next untap
//! step. The second sentence every printed exert card carries -- "when you
//! do" -- is a reflexive trigger, and having one is also what makes a
//! creature exertable at all.
//!
//! Offered as its own action rather than folded into the declaration.
//! Nothing can observe the difference: no player receives priority between
//! declaring an attacker and finishing the declaration, and a trigger
//! captured here waits for the declaration to finish the way every other
//! attack trigger does.

use crate::card::{DeclarativeAbilityDef, TriggerEventDef};
use crate::ids::GameObjectId;

use super::{Action, CommittedTriggerEvent, Game, Permanent, PlayerId};

impl Game {
    /// Whether this permanent prints the second half of exert, which is what
    /// says it may be exerted in the first place.
    fn can_be_exerted(&self, permanent: &Permanent) -> bool {
        self.find_effective_ability(permanent, |effective| {
            effective.ability.is_executable()
                && matches!(
                    effective.ability.definition,
                    DeclarativeAbilityDef::Triggered(triggered)
                        if matches!(triggered.event, TriggerEventDef::Exerted(_))
                )
        })
        .is_some()
    }

    /// "As it attacks": only while the declaration this creature is part of
    /// is still open.
    pub(super) fn exert_actions(&self, player: PlayerId) -> Vec<Action> {
        self.battlefield
            .iter()
            .filter(|permanent| {
                permanent.controller == player
                    && permanent.attacking
                    && !permanent.exerted
                    && self.can_be_exerted(permanent)
            })
            .map(|permanent| Action::ExertAttacker {
                attacker: permanent.card.id,
            })
            .collect()
    }

    pub(super) fn exert_attacker(&mut self, player: PlayerId, attacker: GameObjectId) {
        let exertable = self
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == attacker)
            .is_some_and(|permanent| {
                permanent.controller == player
                    && permanent.attacking
                    && !permanent.exerted
                    && self.can_be_exerted(permanent)
            });
        if !exertable {
            return;
        }
        let Some(permanent) = self
            .battlefield
            .iter_mut()
            .find(|permanent| permanent.card.id == attacker)
        else {
            return;
        };
        permanent.exerted = true;
        // The cost, and the whole of it: one untap step owed, spent whenever
        // that step next comes around.
        permanent.skipped_untap_steps = permanent.skipped_untap_steps.saturating_add(1);
        let Some(object) = self
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == attacker)
            .map(|permanent| self.trigger_event_object(permanent))
        else {
            return;
        };
        self.capture_battlefield_triggers(&CommittedTriggerEvent::Exerted { object });
    }
}
