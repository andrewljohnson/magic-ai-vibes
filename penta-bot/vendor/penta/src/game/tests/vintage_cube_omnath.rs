//! Omnath, Locus of Creation: three different landfalls in one turn, counted
//! by how many times the ability has resolved rather than by how many lands
//! have landed.

use super::*;

/// Player One with an Omnath out since last turn and lands in hand.
fn staged(hand: usize) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let omnath = game
        .put_onto_battlefield(PlayerId::One, cards::OMNATH_LOCUS_OF_CREATION)
        .expect("cataloged");
    for _ in 0..hand {
        let card = game
            .build_zone(PlayerId::One, &[cards::MOUNTAIN])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].hand.push(card);
    }
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [1, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, omnath)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
    game.check_state_based_actions();
}

/// Plays a land from hand, letting the landfall trigger resolve. The
/// one-land-per-turn rule is stepped around directly: what is being measured
/// is the ability, not the land drop.
fn play_a_land(game: &mut Game) {
    game.players[0].lands_played_this_turn = 0;
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { .. }))
        .expect("a land is in hand");
    game.apply(PlayerId::One, action).expect("it plays");
    settle(game);
}

/// The first land of the turn gains four life and nothing else.
#[test]
fn the_first_landfall_gains_four_life() {
    let (mut game, _omnath) = staged(1);
    let before = game.players[0].life;

    play_a_land(&mut game);

    assert_eq!(game.players[0].life, before + 4, "four life");
    assert_eq!(
        game.players[0].mana_pool.total(),
        0,
        "and no mana: the second branch is the one that adds it",
    );
    assert_eq!(game.players[1].life, 20, "nobody was burned");
}

/// The second adds four colours of mana.
#[test]
fn the_second_landfall_adds_four_colors() {
    let (mut game, _omnath) = staged(2);
    play_a_land(&mut game);
    let life = game.players[0].life;
    // Cleared so the four the trigger adds are the only mana in the pool.
    game.players[0].mana_pool = crate::game::ManaPool::default();
    game.players[0].mana.clear();

    play_a_land(&mut game);

    let pool = game.players[0].mana_pool;
    assert_eq!(pool.red, 1, "one red");
    assert_eq!(pool.green, 1, "one green");
    assert_eq!(pool.white, 1, "one white");
    assert_eq!(pool.blue, 1, "one blue");
    assert_eq!(
        game.players[0].life, life,
        "and no more life: the branches are exclusive",
    );
}

/// The third burns each opponent for four.
#[test]
fn the_third_landfall_burns_each_opponent() {
    let (mut game, _omnath) = staged(3);
    play_a_land(&mut game);
    play_a_land(&mut game);
    let life = game.players[0].life;

    play_a_land(&mut game);

    assert_eq!(game.players[1].life, 16, "four damage across the table");
    assert_eq!(
        game.players[0].life, life,
        "and Omnath's controller took none of it",
    );
}

/// A fourth land does nothing at all: three exclusive branches on one count.
#[test]
fn the_fourth_landfall_does_nothing() {
    let (mut game, _omnath) = staged(4);
    play_a_land(&mut game);
    play_a_land(&mut game);
    play_a_land(&mut game);
    let life = game.players[0].life;
    let theirs = game.players[1].life;
    game.players[0].mana_pool = crate::game::ManaPool::default();
    game.players[0].mana.clear();

    play_a_land(&mut game);

    assert_eq!(game.players[0].life, life, "no life");
    assert_eq!(game.players[1].life, theirs, "no damage");
    assert_eq!(game.players[0].mana_pool.total(), 0, "and no mana");
}

/// The count is per turn: next turn's first land gains life again.
#[test]
fn the_count_starts_over_next_turn() {
    let (mut game, _omnath) = staged(3);
    play_a_land(&mut game);
    play_a_land(&mut game);

    game.commit_next_turn(PlayerId::One, Vec::new());
    let life = game.players[0].life;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    play_a_land(&mut game);

    assert_eq!(
        game.players[0].life,
        life + 4,
        "the third land of the game is the first of the turn",
    );
    assert_eq!(game.players[1].life, 20, "and burns nobody");
}

/// Omnath draws a card as it lands, which is the half that makes it worth
/// four colours even when nothing else happens.
#[test]
fn it_draws_a_card_when_it_enters() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let before = game.players[0].library.len();

    game.put_onto_battlefield(PlayerId::One, cards::OMNATH_LOCUS_OF_CREATION)
        .expect("cataloged");
    drain_pending(&mut game);
    settle(&mut game);

    assert_eq!(game.players[0].hand.len(), 1, "one card up");
    assert_eq!(
        game.players[0].library.len(),
        before - 1,
        "off the top of the library",
    );
}
