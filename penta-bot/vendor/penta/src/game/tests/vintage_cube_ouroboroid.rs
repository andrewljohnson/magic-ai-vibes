//! Ouroboroid: a 1/3 that doubles itself every combat and takes the rest of
//! the board with it.

use super::*;

/// The Wurm out since last turn, beside `others`.
fn staged(others: &[(CardDefinitionId, PlayerId)]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let mut ids = Vec::new();
    for (index, (definition, controller)) in others.iter().enumerate() {
        let permanent = creature(
            290_000 + u32::try_from(index).expect("few permanents"),
            *definition,
            *controller,
        );
        ids.push(permanent.card.id);
        game.battlefield.push(permanent);
    }
    let wurm = game
        .put_onto_battlefield(PlayerId::One, cards::OUROBOROID)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, wurm, ids)
}

fn begin_combat(game: &mut Game) {
    game.step = Step::BeginningOfCombat;
    game.begin_step_triggers();
    drain_pending(game);
    game.check_state_based_actions();
}

fn stats(game: &Game, id: GameObjectId) -> (Option<i16>, Option<i16>) {
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield");
    (game.power(permanent), game.toughness(permanent))
}

/// The first combat hands out one counter each, because that is what he is.
#[test]
fn combat_hands_out_his_power_in_counters() {
    let (mut game, wurm, ids) = staged(&[(cards::GRIZZLY_BEARS, PlayerId::One)]);
    let bears = ids[0];

    begin_combat(&mut game);

    assert_eq!(stats(&game, wurm), (Some(2), Some(4)), "he grew himself");
    assert_eq!(stats(&game, bears), (Some(3), Some(3)));
}

/// And the number is read once: everything gets the same amount, however
/// much the Wurm's own counters would have raised it partway through.
#[test]
fn every_creature_gets_the_same_amount() {
    let (mut game, wurm, ids) = staged(&[
        (cards::GRIZZLY_BEARS, PlayerId::One),
        (cards::GRIZZLY_BEARS, PlayerId::One),
    ]);

    begin_combat(&mut game);

    assert_eq!(stats(&game, wurm), (Some(2), Some(4)));
    for bears in ids {
        assert_eq!(stats(&game, bears), (Some(3), Some(3)));
    }
}

/// The next combat is bigger, which is the whole card.
#[test]
fn each_combat_is_bigger_than_the_last() {
    let (mut game, wurm, _ids) = staged(&[]);

    begin_combat(&mut game);
    assert_eq!(stats(&game, wurm), (Some(2), Some(4)));

    begin_combat(&mut game);
    assert_eq!(stats(&game, wurm), (Some(4), Some(6)), "two more this time");

    begin_combat(&mut game);
    assert_eq!(stats(&game, wurm), (Some(8), Some(10)));
}

/// Only your creatures.
#[test]
fn their_creatures_get_nothing() {
    let (mut game, _wurm, ids) = staged(&[(cards::GRIZZLY_BEARS, PlayerId::Two)]);
    let theirs = ids[0];

    begin_combat(&mut game);

    assert_eq!(stats(&game, theirs), (Some(2), Some(2)));
}

/// "On your turn": their combat does nothing.
#[test]
fn their_combat_does_nothing() {
    let (mut game, wurm, _ids) = staged(&[]);
    game.active_player = PlayerId::Two;

    begin_combat(&mut game);

    assert_eq!(stats(&game, wurm), (Some(1), Some(3)));
}
