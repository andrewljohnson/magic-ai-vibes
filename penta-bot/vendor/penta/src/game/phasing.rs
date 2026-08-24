//! Phasing out, and phasing back in.
//!
//! A phased-out permanent is treated as though it does not exist (CR 702.25),
//! so it is held apart from the battlefield rather than left on it behind a
//! flag: every walk over the battlefield is then right without knowing that
//! phasing exists. Phasing is not a zone change, so the permanent keeps its
//! identity, its counters, its damage, and whatever is attached to it, and
//! nothing triggers on it leaving or arriving.

use super::{Game, GameObjectId, PlayerId};

impl Game {
    pub(super) fn phase_out_recipients(
        &mut self,
        recipient: super::EffectRecipientDef,
        object: &super::StackObject,
        context: &super::EffectResolutionContext,
        scoped: super::ScopedEffect,
    ) {
        for target in self.effect_recipients(recipient, object, context, scoped) {
            if let super::Target::Permanent(id) = target {
                self.phase_out(id);
            }
        }
    }

    pub(super) fn phase_out(&mut self, id: GameObjectId) {
        let Some(index) = self
            .battlefield
            .iter()
            .position(|permanent| permanent.card.id == id)
        else {
            return;
        };
        let permanent = self.battlefield.remove(index);
        self.phased_out.push(permanent);
    }

    /// Phases in everything of this player's that is waiting, which happens
    /// before they untap rather than as part of untapping.
    pub(super) fn phase_in_for(&mut self, player: PlayerId) {
        let returning: Vec<usize> = self
            .phased_out
            .iter()
            .enumerate()
            .filter(|(_, permanent)| permanent.controller == player)
            .map(|(index, _)| index)
            .collect();
        for index in returning.into_iter().rev() {
            let permanent = self.phased_out.remove(index);
            self.battlefield.push(permanent);
        }
    }
}
