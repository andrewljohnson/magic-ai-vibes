//! Echo of Eons: a fresh seven, and the flashback nobody stops.

use super::*;

/// Player One holding an Echo, with `mine` and `theirs` as the two hands and
/// a card apiece in the graveyards.
fn staged(mine: &[CardDefinitionId], theirs: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    for seat in [PlayerId::One, PlayerId::Two] {
        game.players[seat.index()].hand.clear();
        game.players[seat.index()].graveyard.clear();
    }
    for (seat, definitions) in [(PlayerId::One, mine), (PlayerId::Two, theirs)] {
        for definition in definitions {
            let card = game
                .build_zone(seat, &[*definition])
                .expect("cataloged")
                .into_iter()
                .next()
                .expect("one card");
            game.players[seat.index()].hand.push(card);
        }
    }
    let card = game
        .build_zone(PlayerId::One, &[cards::ECHO_OF_EONS])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let echo = card.id;
    game.players[0].hand.push(card);
    game.turns_started = [1, 1];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, echo)
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

fn casts(game: &Game, card: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card: id, .. } if *id == card))
        .collect()
}

fn cast(game: &mut Game, card: GameObjectId) {
    let action = casts(game, card)
        .into_iter()
        .next()
        .expect("it is castable");
    game.apply(PlayerId::One, action).expect("it casts");
    settle(game);
}

/// Both hands go back and both players draw seven.
#[test]
fn each_player_shuffles_back_and_draws_seven() {
    let (mut game, echo) = staged(&[cards::MOUNTAIN, cards::ISLAND], &[cards::SWAMP]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);
    let libraries = [game.players[0].library.len(), game.players[1].library.len()];

    cast(&mut game, echo);

    assert_eq!(
        game.players[0].hand.len(),
        7,
        "a fresh seven for the caster"
    );
    assert_eq!(game.players[1].hand.len(), 7, "and for the opponent too");
    // Two cards back and seven drawn on one side, one back and seven drawn on
    // the other: the Echo itself went to the graveyard rather than the library.
    assert_eq!(game.players[0].library.len(), libraries[0] + 2 - 7);
    assert_eq!(game.players[1].library.len(), libraries[1] + 1 - 7);
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::ECHO_OF_EONS),
        "the Echo resolved into its owner's graveyard",
    );
}

/// A graveyard goes back with the hand.
#[test]
fn a_graveyard_goes_back_too() {
    let (mut game, echo) = staged(&[], &[]);
    let buried = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].graveyard.push(buried);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);

    cast(&mut game, echo);

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

/// The flashback is what the card is played for: three mana out of the
/// graveyard, and the Echo is exiled rather than buried again.
#[test]
fn the_flashback_casts_it_from_the_graveyard_and_exiles_it() {
    let (mut game, echo) = staged(&[], &[]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);
    cast(&mut game, echo);
    let buried = game.players[0]
        .graveyard
        .iter()
        .find(|card| card.definition == cards::ECHO_OF_EONS)
        .expect("it is in the graveyard")
        .id;

    assert!(
        casts(&game, buried).is_empty(),
        "not without the flashback cost",
    );
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    cast(&mut game, buried);

    assert_eq!(game.players[0].hand.len(), 7, "another fresh seven");
    assert!(
        !game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::ECHO_OF_EONS),
        "and the Echo did not come back to the graveyard",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::ECHO_OF_EONS),
        "flashback exiled it instead",
    );
}

/// The flashback is three, not six: the printed cost is not what pays for a
/// cast from the graveyard.
#[test]
fn the_graveyard_cast_costs_the_flashback_price() {
    let (mut game, echo) = staged(&[], &[]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);
    cast(&mut game, echo);
    let buried = game.players[0]
        .graveyard
        .iter()
        .find(|card| card.definition == cards::ECHO_OF_EONS)
        .expect("it is in the graveyard")
        .id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert!(
        casts(&game, buried).is_empty(),
        "two mana is one short of the flashback cost",
    );
}
