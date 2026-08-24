// A modal trigger's two placement questions, and that a checkpoint taken at
// either of them rebuilds into the same question. Split out of
// `decisions_and_triggers.rs` for the source-size budget; included
// textually, so the imports here are that module's.

/// A modal trigger is placed in two questions -- which mode, then that
/// mode's targets -- and a checkpoint taken at either point rebuilds into
/// the same question.
#[test]
fn a_modal_trigger_rebuilds_at_both_of_its_questions() {
    let mut game = crate::game::tests::ready_game();
    game.battlefield.clear();
    let kavu = game
        .put_onto_battlefield(PlayerId::One, crate::card::cards::TERRITORIAL_KAVU)
        .expect("cataloged");
    game.players[PlayerId::Two.index()].graveyard.push(
        crate::game::tests::card(9_100, crate::card::cards::SERRA_ANGEL, PlayerId::Two),
    );
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = crate::Step::DeclareAttackers;
    game.priority = PlayerId::One;
    game.declare_attacker(kavu, crate::AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    let priority = game.priority;
    game.apply(priority, Action::PassPriority)
        .expect("priority passes and the trigger is placed");

    // The mode question.
    let viewer = game.decision_player().expect("the mode is asked for");
    let (_, mut rebuilt) = rebuild_current_checkpoint(&game, viewer, 2_101);
    assert!(matches!(
        rebuilt.pending_decisions[0].continuation,
        DecisionContinuation::TriggerMode { .. }
    ));

    // Answering the second mode leaves the target question behind, in both
    // the original and the rebuilt game.
    let decision = rebuilt.pending_decisions[0].observation.id;
    rebuilt.choose_decision(viewer, decision, &[1]);
    let decision = game.pending_decisions[0].observation.id;
    game.choose_decision(viewer, decision, &[1]);
    assert!(matches!(
        game.pending_decisions[0].continuation,
        DecisionContinuation::TriggerPlacement { .. }
    ));

    let (_, rebuilt) = rebuild_current_checkpoint(&game, viewer, 2_102);
    assert!(matches!(
        rebuilt.pending_decisions[0].continuation,
        DecisionContinuation::TriggerPlacement { .. }
    ));
    assert_eq!(
        rebuilt.pending_decisions[0].observation.options.len(),
        game.pending_decisions[0].observation.options.len(),
        "the same graveyard card is offered",
    );
}
