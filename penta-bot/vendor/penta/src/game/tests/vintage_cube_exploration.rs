//! Exploration: a second land drop every turn, and the count that makes one
//! possible at all.

use super::*;

/// A main phase with three lands in hand and nothing played yet.
fn staged(explorations: usize) -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for index in 0..3 {
        game.players[0]
            .hand
            .push(card(92_000 + index, cards::FOREST, PlayerId::One));
    }
    for _ in 0..explorations {
        game.put_onto_battlefield(PlayerId::One, cards::EXPLORATION)
            .expect("cataloged");
    }
    drain_pending(&mut game);
    game.players[0].lands_played_this_turn = 0;
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    game
}

fn land_plays(game: &Game) -> usize {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::PlayLand { .. }))
        .count()
}

fn play_a_land(game: &mut Game) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { .. }))
        .expect("a land is offered");
    game.apply(PlayerId::One, action).expect("it is played");
    drain_pending(game);
}

/// Without it, one land is the whole turn's allowance.
#[test]
fn one_land_a_turn_without_it() {
    let mut game = staged(0);

    play_a_land(&mut game);

    assert_eq!(land_plays(&game), 0, "the drop is spent");
}

/// With it, a second land is still on offer.
#[test]
fn it_allows_a_second_land() {
    let mut game = staged(1);

    play_a_land(&mut game);

    assert!(land_plays(&game) > 0, "the second drop is there");
}

/// And only a second: the third is not.
#[test]
fn it_allows_exactly_one_more() {
    let mut game = staged(1);

    play_a_land(&mut game);
    play_a_land(&mut game);

    assert_eq!(land_plays(&game), 0, "one additional land, not two");
}

/// Two copies are two extra lands, which is why the rule counts.
#[test]
fn two_copies_allow_two_more() {
    let mut game = staged(2);

    play_a_land(&mut game);
    play_a_land(&mut game);

    assert!(land_plays(&game) > 0, "the third drop is there");
}

/// The allowance is per turn, so a new turn hands it back.
#[test]
fn the_allowance_returns_each_turn() {
    let mut game = staged(1);
    play_a_land(&mut game);
    play_a_land(&mut game);
    assert_eq!(land_plays(&game), 0);

    game.players[0].lands_played_this_turn = 0;

    assert!(land_plays(&game) > 0, "each of your turns");
}

/// It is your allowance, not theirs.
#[test]
fn it_does_not_help_the_opponent() {
    let mut game = staged(1);
    game.players[1].hand.clear();
    game.players[1]
        .hand
        .push(card(92_100, cards::FOREST, PlayerId::Two));
    game.players[1].lands_played_this_turn = 1;
    game.active_player = PlayerId::Two;
    game.priority = PlayerId::Two;

    assert!(
        game.legal_actions(PlayerId::Two)
            .into_iter()
            .all(|action| !matches!(action, Action::PlayLand { .. })),
        "their drop is spent, and your enchantment is yours",
    );
}

/// Losing it closes the second drop again.
#[test]
fn losing_it_closes_the_second_drop() {
    let mut game = staged(1);
    let exploration = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::EXPLORATION)
        .map(|permanent| permanent.card.id)
        .expect("it is there");
    play_a_land(&mut game);
    assert!(land_plays(&game) > 0);

    game.move_permanents_to_graveyard(&[exploration]);
    drain_pending(&mut game);

    assert_eq!(land_plays(&game), 0, "the permission went with it");
}
