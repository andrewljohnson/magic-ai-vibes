//! The spells paid for by returning lands, and the one paid for by exiling a
//! card.
//!
//! What separates these from an ordinary additional cost is where the spent
//! objects end up: the lands come back to hand rather than dying, and
//! Pyrokinesis exiles rather than discards. Each test checks the destination,
//! not merely that something was spent.

use super::*;

fn ready() -> Game {
    let mut game = ready_game();
    game.turn = 5;
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::One.index()].graveyard.clear();
    game
}

fn settle(game: &mut Game) {
    for _ in 0..12 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// Islands on the battlefield, and the free cast of `spell` if one is
/// offered. A counterspell needs something to point at, so the stack always
/// holds one.
fn free_cast(spell: CardDefinitionId, islands: usize) -> (Game, Option<Action>) {
    let mut game = ready();
    game.stack.push(crate::game::tests::spell(
        21_000,
        cards::GRIZZLY_BEARS,
        PlayerId::Two,
        0,
    ));
    for index in 0..islands {
        game.battlefield.push(creature(
            10_000 + u32::try_from(index).expect("small"),
            cards::ISLAND,
            PlayerId::One,
        ));
    }
    let card_in_hand = card(20_000, spell, PlayerId::One);
    let spell_id = card_in_hand.id;
    game.players[PlayerId::One.index()].hand.push(card_in_hand);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, choices, .. }
                if *card == spell_id && choices.costs().alternative().is_some())
        });
    (game, cast)
}

#[test]
fn gush_is_free_with_two_islands() {
    let (_, cast) = free_cast(cards::GUSH, 2);
    assert!(cast.is_some(), "two Islands pay for it with no mana at all");
}

#[test]
fn gush_needs_both_islands() {
    let (_, cast) = free_cast(cards::GUSH, 1);
    assert!(cast.is_none(), "one Island is not two");
}

/// The lands come back rather than dying, which is the whole difference
/// between this cost and a sacrifice.
#[test]
fn gush_returns_its_islands_to_hand() {
    let (mut game, cast) = free_cast(cards::GUSH, 2);
    game.apply(PlayerId::One, cast.expect("the free cast is offered"))
        .expect("it is cast");
    settle(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::ISLAND),
        "both Islands left the battlefield",
    );
    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .filter(|card| card.definition == cards::ISLAND)
            .count(),
        2,
        "and both are in hand, not in the graveyard",
    );
    assert!(
        game.players[PlayerId::One.index()]
            .graveyard
            .iter()
            .all(|card| { card.definition != cards::ISLAND }),
        "nothing was sacrificed",
    );
}

#[test]
fn thwart_wants_three_islands() {
    assert!(free_cast(cards::THWART, 2).1.is_none(), "two is not three");
    assert!(free_cast(cards::THWART, 3).1.is_some(), "three pays for it");
}

#[test]
fn daze_wants_one_island() {
    assert!(
        free_cast(cards::DAZE, 0).1.is_none(),
        "no Island, no free Daze"
    );
    assert!(
        free_cast(cards::DAZE, 1).1.is_some(),
        "one Island is enough"
    );
}

/// Pyrokinesis exiles the red card it spends: it never becomes a graveyard
/// card, which matters to everything that reads a graveyard.
#[test]
fn pyrokinesis_exiles_the_card_it_spends() {
    let mut game = ready();
    game.battlefield
        .push(creature(10_000, cards::SERRA_ANGEL, PlayerId::Two));
    let pyro = card(20_000, cards::PYROKINESIS, PlayerId::One);
    let pyro_id = pyro.id;
    game.players[PlayerId::One.index()].hand.push(pyro);
    game.players[PlayerId::One.index()].hand.push(card(
        20_001,
        cards::LIGHTNING_BOLT,
        PlayerId::One,
    ));

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, choices, .. }
                if *card == pyro_id && choices.costs().alternative().is_some())
        })
        .expect("a red card in hand pays for it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert_eq!(
        game.players[PlayerId::One.index()].exile.len(),
        1,
        "the red card was exiled",
    );
    assert!(
        game.players[PlayerId::One.index()]
            .graveyard
            .iter()
            .all(|card| card.definition != cards::LIGHTNING_BOLT),
        "and not discarded",
    );
}

/// With no red card in hand there is nothing to exile, so the free cast is
/// not offered at all.
#[test]
fn pyrokinesis_needs_a_red_card_to_exile() {
    let mut game = ready();
    let pyro = card(20_000, cards::PYROKINESIS, PlayerId::One);
    let pyro_id = pyro.id;
    game.players[PlayerId::One.index()].hand.push(pyro);
    game.players[PlayerId::One.index()].hand.push(card(
        20_001,
        cards::ANCESTRAL_RECALL,
        PlayerId::One,
    ));

    let free = game.legal_actions(PlayerId::One).into_iter().any(|action| {
        matches!(action, Action::CastSpell { card, choices, .. }
            if card == pyro_id && choices.costs().alternative().is_some())
    });
    assert!(!free, "a blue card is not a red card");
}
