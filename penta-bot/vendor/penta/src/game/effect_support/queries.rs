//! Resolving an object query into the objects it matches.
//!
//! Split out of the parent module for the source-size budget.

#![allow(clippy::wildcard_imports)]

use super::*;

impl Game {
    /// Finds objects using only zone, relation, and effective-characteristic
    /// predicates. Unlike target enumeration, this does not apply hexproof,
    /// protection, or any other targeting restriction.
    pub(in crate::game) fn objects_matching_query(
        &self,
        query: ObjectQueryDef,
        evaluation_controller: PlayerId,
        source: GameObjectId,
        context: TriggerContext,
    ) -> Vec<Target> {
        self.objects_matching_query_with_prospective(
            query,
            evaluation_controller,
            source,
            context,
            None,
        )
    }

    pub(in crate::game) fn objects_matching_query_with_prospective(
        &self,
        query: ObjectQueryDef,
        evaluation_controller: PlayerId,
        source: GameObjectId,
        context: TriggerContext,
        prospective: Option<&Permanent>,
    ) -> Vec<Target> {
        let mut recipients = Vec::new();
        let result = self.visit_objects_matching_query_with_prospective(
            query,
            evaluation_controller,
            source,
            context,
            prospective,
            |recipient| {
                recipients.push(recipient);
                ControlFlow::Continue(())
            },
        );
        debug_assert!(result.is_continue());
        recipients
    }

    pub(in crate::game) fn any_object_matches_query_with_prospective(
        &self,
        query: ObjectQueryDef,
        evaluation_controller: PlayerId,
        source: GameObjectId,
        context: TriggerContext,
        prospective: Option<&Permanent>,
    ) -> bool {
        self.visit_objects_matching_query_with_prospective(
            query,
            evaluation_controller,
            source,
            context,
            prospective,
            |_| ControlFlow::Break(()),
        )
        .is_break()
    }

    pub(in crate::game) fn visit_objects_matching_query_with_prospective(
        &self,
        query: ObjectQueryDef,
        evaluation_controller: PlayerId,
        source: GameObjectId,
        context: TriggerContext,
        prospective: Option<&Permanent>,
        mut visitor: impl FnMut(Target) -> ControlFlow<()>,
    ) -> ControlFlow<()> {
        if query.zones.contains(&ZoneKind::Battlefield) {
            for permanent in &self.battlefield {
                if !self.player_relation_matches(
                    permanent.controller,
                    query.controller,
                    evaluation_controller,
                    context,
                ) {
                    continue;
                }
                let characteristics = prospective.map_or_else(
                    || self.trigger_event_object(permanent),
                    |prospective| {
                        self.trigger_event_object_with_prospective(permanent, prospective)
                    },
                );
                if self.trigger_object_matches(query.object, &characteristics, source, false)
                    && visitor(Target::Permanent(permanent.card.id)).is_break()
                {
                    return ControlFlow::Break(());
                }
            }
        }
        if query.zones.contains(&ZoneKind::Stack) {
            for candidate in self.stack.iter() {
                if candidate.kind != StackObjectKind::Spell
                    || !self.player_relation_matches(
                        candidate.controller,
                        query.controller,
                        evaluation_controller,
                        context,
                    )
                {
                    continue;
                }
                let Some(characteristics) = self.stack_trigger_event_object(candidate) else {
                    continue;
                };
                if self.trigger_object_matches(query.object, &characteristics, source, true)
                    && visitor(Target::Spell(candidate.id)).is_break()
                {
                    return ControlFlow::Break(());
                }
            }
        }
        // The same card zones the target enumerator understands. Without this
        // a sweep over graveyards matched nothing and the clause was inert.
        for zone in [
            ZoneKind::Library,
            ZoneKind::Hand,
            ZoneKind::Graveyard,
            ZoneKind::Exile,
            ZoneKind::Command,
        ] {
            if !query.zones.contains(&zone) {
                continue;
            }
            for card in self.cards_in_zone(zone) {
                if self.player_relation_matches(
                    card.owner,
                    query.controller,
                    evaluation_controller,
                    context,
                ) && self.card_object_matches(query.object, card, zone, source)
                    && visitor(Target::Card(card.id)).is_break()
                {
                    return ControlFlow::Break(());
                }
            }
        }
        ControlFlow::Continue(())
    }
}
