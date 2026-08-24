//! Time Spiral: a wheel that gives the mana back, so the seven new cards
//! arrive castable.

use super::*;

/// Player One holding a Time Spiral with the six mana to cast it, `lands`
/// tapped on their side and `theirs` tapped on the opponent's.
fn staged(lands: usize, theirs: usize) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    for seat in [PlayerId::One, PlayerId::Two] {
        game.players[seat.index()].hand.clear();
        game.players[seat.index()].graveyard.clear();
    }
    for (seat, count) in [(PlayerId::One, lands), (PlayerId::Two, theirs)] {
        for _ in 0..count {
            game.put_onto_battlefield(seat, cards::ISLAND)
                .expect("cataloged");
        }
    }
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.tapped = true;
    }
    let card = game
        .build_zone(PlayerId::One, &[cards::TIME_SPIRAL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let spiral = card.id;
    game.players[0].hand.push(card);
    game.turns_started = [2, 1];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);
    (game, spiral)
}

fn deciding(game: &Game) -> Option<PlayerId> {
    game.pending_decisions
        .first()
        .map(|pending| pending.observation.player)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if deciding(game).is_some() {
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

fn cast(game: &mut Game, spiral: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == spiral))
        .expect("six mana casts it");
    game.apply(PlayerId::One, action).expect("it casts");
    settle(game);
}

/// Answers the untap choice with the first `count` lands on offer.
fn untap(game: &mut Game, count: usize) {
    let seat = deciding(game).expect("it asks which lands to untap");
    let decision = game.observe(seat).decision.expect("just checked");
    let options = decision
        .options
        .iter()
        .take(count)
        .map(|option| option.id)
        .collect();
    game.apply(
        seat,
        Action::ChooseDecision {
            decision: decision.id,
            options,
        },
    )
    .expect("the answer is legal");
    settle(game);
}

fn untapped(game: &Game, seat: PlayerId) -> usize {
    game.battlefield
        .iter()
        .filter(|permanent| permanent.controller == seat && !permanent.tapped)
        .count()
}

/// Both players wheel, and the Spiral is exiled rather than buried.
#[test]
fn it_wheels_both_players_and_exiles_itself() {
    let (mut game, spiral) = staged(2, 0);
    let held = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].hand.push(held);

    cast(&mut game, spiral);
    untap(&mut game, 0);

    assert_eq!(
        game.players[0].hand.len(),
        7,
        "a fresh seven for the caster"
    );
    assert_eq!(game.players[1].hand.len(), 7, "and for the opponent too");
    assert!(
        game.players[0].graveyard.is_empty(),
        "the Spiral did not go to a graveyard",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::TIME_SPIRAL),
        "it exiled itself instead",
    );
}

/// The untap is what makes the wheel free: six lands come back.
#[test]
fn it_untaps_up_to_six_lands() {
    let (mut game, spiral) = staged(6, 0);
    cast(&mut game, spiral);
    assert_eq!(untapped(&game, PlayerId::One), 0, "all six were tapped");

    untap(&mut game, 6);

    assert_eq!(
        untapped(&game, PlayerId::One),
        6,
        "and all six came back up",
    );
}

/// "Up to six": a seventh land stays down.
#[test]
fn it_untaps_no_more_than_six() {
    let (mut game, spiral) = staged(7, 0);
    cast(&mut game, spiral);

    let seat = deciding(&game).expect("it asks");
    assert_eq!(
        game.observe(seat).decision.expect("just checked").maximum,
        6,
        "six is the cap however many lands are down",
    );
    untap(&mut game, 6);
    assert_eq!(untapped(&game, PlayerId::One), 6, "six up, one still down");
}

/// "Up to" means none is an answer.
#[test]
fn untapping_nothing_is_allowed() {
    let (mut game, spiral) = staged(3, 0);
    cast(&mut game, spiral);
    let seat = deciding(&game).expect("it asks");
    assert_eq!(
        game.observe(seat).decision.expect("just checked").minimum,
        0,
        "with no obligation to untap any",
    );

    untap(&mut game, 0);

    assert_eq!(untapped(&game, PlayerId::One), 0, "none came up");
    assert_eq!(
        game.players[0].hand.len(),
        7,
        "and the wheel still happened"
    );
}

/// The lands are not targeted and need not be yours, which is what the
/// clause leaves out.
#[test]
fn their_lands_are_on_the_menu_too() {
    let (mut game, spiral) = staged(0, 2);
    cast(&mut game, spiral);

    let seat = deciding(&game).expect("it asks");
    assert_eq!(
        game.observe(seat)
            .decision
            .expect("just checked")
            .options
            .len(),
        2,
        "their two Islands are the only lands there are",
    );
    untap(&mut game, 2);
    assert_eq!(
        untapped(&game, PlayerId::Two),
        2,
        "and untapping them is legal",
    );
}

/// A graveyard goes back with the hand.
#[test]
fn a_graveyard_goes_back_too() {
    let (mut game, spiral) = staged(1, 0);
    let buried = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].graveyard.push(buried);

    cast(&mut game, spiral);
    untap(&mut game, 0);

    assert!(
        game.players[1].graveyard.is_empty(),
        "their graveyard was shuffled away",
    );
    assert_eq!(
        game.players[1]
            .library
            .iter()
            .filter(|card| card.definition == cards::SERRA_ANGEL)
            .count(),
        1,
        "and the Angel is somewhere in their library",
    );
}
