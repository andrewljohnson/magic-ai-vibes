//! Committing a crime (CR 701.51a).
//!
//! A player commits a crime as they cast a spell, activate an ability, or
//! put a triggered ability onto the stack that targets an opponent, anything
//! an opponent controls, or a card in an opponent's graveyard. It is one
//! event however many of those a single spell points at, and it happens as
//! the targets are locked in rather than when anything resolves.

use super::{CommittedTriggerEvent, Game, PlayerId, Target, ZoneKind};

impl Game {
    /// Publishes the crime, if these targets amount to one.
    pub(super) fn capture_crime_triggers(&mut self, player: PlayerId, targets: &[Target]) {
        if !targets
            .iter()
            .any(|target| self.target_belongs_to_opponent(player, *target))
        {
            return;
        }
        self.capture_battlefield_triggers(&CommittedTriggerEvent::CommittedCrime { player });
    }

    fn target_belongs_to_opponent(&self, player: PlayerId, target: Target) -> bool {
        let opponent = player.opponent();
        match target {
            Target::Player(targeted) => targeted == opponent,
            // A permanent or a spell is an opponent's when they control it.
            Target::Permanent(id) | Target::Spell(id) => self
                .current_or_last_known_controller(id)
                .is_some_and(|controller| controller == opponent),
            // A card is one when it sits in their graveyard. Cards anywhere
            // else -- a library, an exile pile -- are not named by the rule.
            Target::Card(id) => self
                .card_in_nonbattlefield_zone(id)
                .is_some_and(|(zone, card)| zone == ZoneKind::Graveyard && card.owner == opponent),
        }
    }
}
