//! Voting, and Council's Judgment, which is how it reaches the cube.

use super::*;

/// Answers the pending vote for `voter` by choosing `choice`.
fn vote_for(game: &mut Game, voter: PlayerId, choice: GameObjectId) {
    let decision = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("a vote is pending");
    assert_eq!(decision.player, voter, "the wrong player is being asked");
    let option = decision
        .options
        .iter()
        .find(|option| option.card.is_some_and(|(card, _)| card == choice))
        .expect("that permanent is on the ballot");
    game.apply(
        voter,
        Action::ChooseDecision {
            decision: decision.id,
            options: vec![option.id],
        },
    )
    .expect("the vote is cast");
}

/// Casts the Judgment and settles everything up to the first vote.
fn cast_judgment(game: &mut Game) {
    let judgment = card(87_000, cards::COUNCILS_JUDGMENT, PlayerId::One);
    let judgment_id = judgment.id;
    game.players[0].hand.push(judgment);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == judgment_id))
        .expect("three mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    pass_until_decision(game);
}

/// Agreeing exiles one permanent. The ballot holds only what the caster does
/// not control, so their own creature is never a candidate.
#[test]
fn agreeing_exiles_the_one_permanent() {
    let mut game = ready_game();
    game.battlefield.clear();
    let mine = creature(87_010, cards::SAVANNAH_LIONS, PlayerId::One);
    let mine_id = mine.card.id;
    game.battlefield.push(mine);
    let bears = creature(87_011, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let angel = creature(87_012, cards::SERRA_ANGEL, PlayerId::Two);
    let angel_id = angel.card.id;
    game.battlefield.push(angel);

    cast_judgment(&mut game);
    let ballot = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the first vote is pending");
    assert!(
        ballot
            .options
            .iter()
            .all(|option| option.card.is_some_and(|(card, _)| card != mine_id)),
        "you never vote for your own permanent",
    );

    vote_for(&mut game, PlayerId::One, angel_id);
    vote_for(&mut game, PlayerId::Two, angel_id);
    drain_pending(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == angel_id),
        "the Angel had both votes",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears_id),
        "and the Bears had none",
    );
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL),
        "exiled rather than destroyed",
    );
}

/// Disagreeing ties, and everything tied for most votes is exiled -- which
/// is why the card is a two-for-one against a real board.
#[test]
fn disagreeing_exiles_both() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = creature(87_020, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let angel = creature(87_021, cards::SERRA_ANGEL, PlayerId::Two);
    let angel_id = angel.card.id;
    game.battlefield.push(angel);

    cast_judgment(&mut game);
    vote_for(&mut game, PlayerId::One, angel_id);
    vote_for(&mut game, PlayerId::Two, bears_id);
    drain_pending(&mut game);

    assert!(
        game.battlefield.is_empty(),
        "one vote each is a tie, and a tie takes both",
    );
}

/// With nothing to vote for, the spell resolves and does nothing.
#[test]
fn an_empty_ballot_asks_nothing() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.battlefield
        .push(creature(87_030, cards::SAVANNAH_LIONS, PlayerId::One));

    cast_judgment(&mut game);

    assert!(
        game.pending_decisions.is_empty(),
        "nobody is asked to vote for nothing",
    );
    assert_eq!(game.battlefield.len(), 1, "and your own creature is safe");
}
