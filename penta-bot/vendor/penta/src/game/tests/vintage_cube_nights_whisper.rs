//! Night's Whisper: two cards for two mana and two life, which is a rate
//! only black is offered.

use super::*;

/// Player One holding a Whisper with two mana up.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let card = game
        .build_zone(PlayerId::One, &[cards::NIGHTS_WHISPER])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let whisper = card.id;
    game.players[0].hand.push(card);
    game.turns_started = [1, 1];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    (game, whisper)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if !game.pending_decisions.is_empty() {
            return;
        }
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

fn cast(game: &mut Game, whisper: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == whisper))
        .expect("two mana casts it");
    game.apply(PlayerId::One, action).expect("it casts");
    settle(game);
}

/// Two cards in, two life out.
#[test]
fn it_draws_two_and_costs_two_life() {
    let (mut game, whisper) = staged();
    let hand = game.players[0].hand.len();
    let library = game.players[0].library.len();

    cast(&mut game, whisper);

    assert_eq!(
        game.players[0].hand.len(),
        hand - 1 + 2,
        "the Whisper left and two came back",
    );
    assert_eq!(game.players[0].library.len(), library - 2, "off the top");
    assert_eq!(game.players[0].life, 18, "and two life for them");
}

/// It is your own life and your own cards: the opponent is untouched.
#[test]
fn it_touches_nobody_else() {
    let (mut game, whisper) = staged();
    let theirs = game.players[1].hand.len();

    cast(&mut game, whisper);

    assert_eq!(game.players[1].life, 20, "their life is their own");
    assert_eq!(game.players[1].hand.len(), theirs, "and so is their hand");
}

/// The life is not a cost, so nothing about it gates the cast: a player at
/// two casts it, draws the cards, and loses.
#[test]
fn the_life_is_an_effect_rather_than_a_cost() {
    let (mut game, whisper) = staged();
    game.players[0].life = 2;
    let library = game.players[0].library.len();

    cast(&mut game, whisper);

    assert_eq!(game.players[0].life, 0, "down to nothing");
    assert_eq!(
        game.players[0].library.len(),
        library - 2,
        "with the two cards drawn all the same",
    );
    assert!(game.result.is_some(), "and a player at zero life has lost");
}
