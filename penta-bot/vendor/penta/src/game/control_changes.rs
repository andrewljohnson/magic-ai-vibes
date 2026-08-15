//! Taking control of a permanent, for a turn or for as long as something
//! holds it.
//!
//! Both printed forms share a body; they differ only in what ends them, which
//! is recorded on the permanent and checked elsewhere -- cleanup for the
//! turn-scoped form, state-based actions for the held one.

use super::{
    EffectRecipientDef, Game, GameObjectId, ScopedEffect, StackObject, Target, TriggerContext,
};

impl Game {
    /// The shared body of both control-change effects. `holder` is the
    /// permanent whose presence sustains the change and whether it also has
    /// to stay tapped, or `None` for the turn-scoped form cleanup ends.
    pub(super) fn take_control_of(
        &mut self,
        recipient: EffectRecipientDef,
        object: &StackObject,
        context: TriggerContext,
        scoped: ScopedEffect,
        holder: Option<(GameObjectId, bool)>,
    ) {
        let controller = object.controller;
        for target in self.effect_recipients(recipient, object, context, scoped) {
            let Target::Permanent(id) = target else {
                continue;
            };
            let Some(index) = self
                .battlefield
                .iter()
                .position(|permanent| permanent.card.id == id)
            else {
                continue;
            };
            if self.battlefield[index].controller == controller
                || self.cannot_change_controller(&self.battlefield[index])
            {
                continue;
            }
            let permanent = &mut self.battlefield[index];
            // Only the first change records where control came from, so
            // passing a permanent around and back still returns it to whoever
            // had it before the turn started.
            permanent
                .control_reverts_to
                .get_or_insert(permanent.controller);
            permanent.controller = controller;
            permanent.control_source = holder.map(|(id, _)| id);
            permanent.control_requires_source_tapped = holder.is_some_and(|(_, tapped)| tapped);
            // It has not been under its new controller's control since their
            // turn began, so it is summoning sick unless something grants
            // haste. This is why the cards that steal a creature almost always
            // grant it too.
            permanent.entered_controller_turn = self.turns_started[controller.index()];
        }
    }
}
