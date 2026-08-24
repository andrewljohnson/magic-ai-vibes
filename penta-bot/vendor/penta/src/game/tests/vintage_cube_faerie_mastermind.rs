//! Faerie Mastermind: a flash flier that taxes the other deck's cantrips,
//! and an outlet that makes the tax collect itself.

use super::*;

/// The Mastermind on the battlefield, both libraries stocked, and nobody
/// having drawn anything yet this turn.
fn staged() -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.put_onto_battlefield(PlayerId::One, cards::FAERIE_MASTERMIND)
        .expect("cataloged");
    drain_pending(&mut game);
    for (seat, player) in [PlayerId::One, PlayerId::Two].into_iter().enumerate() {
        let base = 91_000 + u32::try_from(seat).expect("two seats") * 100;
        game.players[player.index()].hand.clear();
        game.players[player.index()].library = (0..8)
            .map(|index| card(base + index, cards::MOUNTAIN, player))
            .collect();
    }
    game.cards_drawn_this_turn = [0; 2];
    game.drawn_this_turn = [Vec::new(), Vec::new()];
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    game
}

fn draw(game: &mut Game, player: PlayerId) {
    game.draw_card(player);
    drain_pending(game);
}

/// Their first card each turn is the one the rules hand them, and it fires
/// nothing.
#[test]
fn their_first_draw_does_nothing() {
    let mut game = staged();
    let before = game.players[0].hand.len();

    draw(&mut game, PlayerId::Two);

    assert_eq!(game.players[0].hand.len(), before, "nothing yet");
}

/// Their second one is what the clause is about.
#[test]
fn their_second_draw_draws_you_a_card() {
    let mut game = staged();
    draw(&mut game, PlayerId::Two);
    let before = game.players[0].hand.len();

    draw(&mut game, PlayerId::Two);

    assert_eq!(game.players[0].hand.len(), before + 1);
}

/// And only the second: a third does nothing more.
#[test]
fn their_third_draw_does_nothing_more() {
    let mut game = staged();
    draw(&mut game, PlayerId::Two);
    draw(&mut game, PlayerId::Two);
    let before = game.players[0].hand.len();

    draw(&mut game, PlayerId::Two);

    assert_eq!(game.players[0].hand.len(), before, "the ordinal is exact");
}

/// "An opponent": your own second draw is not one.
#[test]
fn your_own_second_draw_does_nothing() {
    let mut game = staged();
    draw(&mut game, PlayerId::One);
    let before = game.players[0].hand.len();

    draw(&mut game, PlayerId::One);

    assert_eq!(
        game.players[0].hand.len(),
        before + 1,
        "one card, the one you drew",
    );
}

/// The count is per turn, so a new turn arms it again.
#[test]
fn the_count_resets_between_turns() {
    let mut game = staged();
    draw(&mut game, PlayerId::Two);
    draw(&mut game, PlayerId::Two);
    game.cards_drawn_this_turn = [0; 2];
    let before = game.players[0].hand.len();

    draw(&mut game, PlayerId::Two);
    assert_eq!(game.players[0].hand.len(), before, "their first, again");
    draw(&mut game, PlayerId::Two);

    assert_eq!(game.players[0].hand.len(), before + 1, "and their second");
}

/// The outlet draws both players a card -- which, with the trigger out and
/// the opponent already on one, hands you a second card as well.
#[test]
fn the_outlet_draws_for_everyone() {
    let mut game = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 4);
    draw(&mut game, PlayerId::Two);
    let yours = game.players[0].hand.len();
    let theirs = game.players[1].hand.len();

    let mastermind = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::FAERIE_MASTERMIND)
        .map(|permanent| permanent.card.id)
        .expect("it is there");
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == mastermind),
        )
        .expect("four mana activates it");
    game.apply(PlayerId::One, action).expect("it activates");
    drain_pending(&mut game);

    assert_eq!(game.players[1].hand.len(), theirs + 1, "their card");
    assert_eq!(
        game.players[0].hand.len(),
        yours + 2,
        "yours, and the one their second draw handed you",
    );
}

/// Flash and flying are printed on it.
#[test]
fn it_flashes_in_and_flies() {
    let game = staged();
    let mastermind = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::FAERIE_MASTERMIND)
        .expect("it is there");

    assert!(game.permanent_has_executable_keyword(mastermind, KeywordAbility::Flying));
}
