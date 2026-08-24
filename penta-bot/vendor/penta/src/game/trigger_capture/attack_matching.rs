// Whether a committed attack event answers a printed attack matcher, and
// the same question for a move of cards into exile.
//
// Split from the capture logic because these are different questions: one is
// about a creature that attacked, one about the declaration as a whole, and
// one about a whole move rather than any card in it. Included textually into
// `trigger_capture.rs`, so the imports here are the parent module's.

/// What one attack declaration published about one attacker: how big the
/// declaration was, how many times this creature has attacked this turn, and
/// who it was aimed at. Three facts of the same event, carried together so
/// the matcher below reads one argument rather than three.
#[derive(Clone, Copy)]
pub(in crate::game) struct AttackEventFacts {
    pub(in crate::game) declaration_size: u8,
    pub(in crate::game) attack_number: u8,
    pub(in crate::game) defending_player: PlayerId,
}

impl Game {
    /// "Whenever one or more cards are put into exile from ...". Counted
    /// rather than matched: the move is the event, and what the clause asks
    /// is only which zone the cards came out of and whose it was.
    fn exile_move_matches(
        &self,
        zones: &'static [crate::card::ZoneKind],
        owner: PlayerRelation,
        cards: &[TriggerEventObject],
        from: crate::card::ZoneKind,
        exiled_by: PlayerId,
        controller: Option<PlayerId>,
    ) -> bool {
        !cards.is_empty()
            && zones.contains(&from)
            && self.player_relation_matches(
                exiled_by,
                owner,
                controller.unwrap_or(exiled_by),
                TriggerContext::empty(),
            )
    }

    /// "Whenever you attack." Counted rather than matched: the declaration is
    /// the event, and the predicate says which of the creatures in it count
    /// toward the size the clause asks for.
    fn attack_declaration_matches(
        &self,
        attacker: ObjectPredicateDef,
        declaration: AttackDeclarationRangeDef,
        attackers: &[TriggerEventObject],
        source: GameObjectId,
        controller: Option<PlayerId>,
    ) -> bool {
        let matching = attackers
            .iter()
            .filter(|object| {
                self.trigger_object_matches_for_controller(
                    attacker, object, source, false, controller,
                )
            })
            .count();
        let matching = u8::try_from(matching).unwrap_or(u8::MAX);
        matching >= declaration.minimum
            && declaration
                .maximum
                .is_none_or(|maximum| matching <= maximum)
    }

    /// "Whenever this creature attacks", and the clauses that narrow it by
    /// how big the declaration was or how many times this creature has
    /// attacked. Both numbers are facts of the event rather than conditions
    /// to recheck while the trigger is placed.
    fn attacker_matches(
        &self,
        matcher: AttackEventMatcherDef,
        object: &TriggerEventObject,
        attack: AttackEventFacts,
        source: GameObjectId,
        controller: Option<PlayerId>,
    ) -> bool {
        let AttackEventFacts {
            declaration_size,
            attack_number,
            defending_player,
        } = attack;
        // Who is being attacked, for the clauses that ask. The event already
        // resolves a planeswalker to the player who controls it, so "attacks
        // you or a planeswalker you control" is one comparison.
        if let Some(defender) = matcher.defender
            && !controller.is_some_and(|controller| {
                self.player_relation_matches(
                    defending_player,
                    defender,
                    controller,
                    crate::game::TriggerContext::empty(),
                )
            })
        {
            return false;
        }
        declaration_size >= matcher.declaration.minimum
            && matcher
                .declaration
                .maximum
                .is_none_or(|maximum| declaration_size <= maximum)
            && matcher
                .attack_number
                .is_none_or(|number| attack_number == number)
            && self.trigger_object_matches_for_controller(
                matcher.attacker,
                object,
                source,
                false,
                controller,
            )
    }
}
