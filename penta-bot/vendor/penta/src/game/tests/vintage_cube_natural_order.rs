//! Natural Order: a green creature traded for whichever green creature the
//! deck is built around.

use super::*;

/// A main phase with Natural Order in hand, four mana, and a library that
/// holds one green fatty and one thing that is not a legal find.
fn staged(with_green_creature: bool) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    game.players[0]
        .library
        .push(card(90_000, cards::LIGHTNING_BOLT, PlayerId::One));
    game.players[0]
        .library
        .push(card(90_001, cards::WORLDSPINE_WURM, PlayerId::One));
    let order = card(90_010, cards::NATURAL_ORDER, PlayerId::One);
    let order_id = order.id;
    game.players[0].hand.push(order);
    if with_green_creature {
        game.put_onto_battlefield(PlayerId::One, cards::LLANOWAR_ELVES)
            .expect("cataloged");
        drain_pending(&mut game);
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, order_id)
}

fn cast_actions(game: &Game, order: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card, .. } if *card == order))
        .collect()
}

/// The sacrifice is a cost, so an empty board cannot cast it at all.
#[test]
fn it_is_uncastable_without_a_green_creature() {
    let (game, order) = staged(false);

    assert!(
        cast_actions(&game, order).is_empty(),
        "there is nothing to pay the additional cost with",
    );
}

/// A green creature on the battlefield is what makes it castable, and it is
/// what gets sacrificed.
#[test]
fn casting_it_sacrifices_the_green_creature() {
    let (mut game, order) = staged(true);
    let action = cast_actions(&game, order)
        .into_iter()
        .next()
        .expect("the Elves pay for it");

    game.apply(PlayerId::One, action)
        .expect("the cast is legal");
    drain_pending(&mut game);

    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::LLANOWAR_ELVES),
        "the Elves were sacrificed as a cost",
    );
}

/// The search finds a green creature card and puts it onto the battlefield.
#[test]
fn it_puts_a_green_creature_onto_the_battlefield() {
    let (mut game, order) = staged(true);
    let action = cast_actions(&game, order)
        .into_iter()
        .next()
        .expect("the Elves pay for it");
    game.apply(PlayerId::One, action)
        .expect("the cast is legal");
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::WORLDSPINE_WURM),
        "the Wurm is on the battlefield, not in hand",
    );
    assert!(
        game.players[0]
            .library
            .iter()
            .all(|card| card.definition != cards::WORLDSPINE_WURM),
        "and out of the library",
    );
}

/// It is a green *creature* card: nothing else in the library is on offer.
#[test]
fn the_search_names_green_creature_cards_only() {
    let (mut game, order) = staged(true);
    let action = cast_actions(&game, order)
        .into_iter()
        .next()
        .expect("the Elves pay for it");
    game.apply(PlayerId::One, action)
        .expect("the cast is legal");
    pass_until_decision(&mut game);

    let search = game
        .observe(PlayerId::One)
        .decision
        .expect("resolving offers the search");
    let offered = search
        .options
        .iter()
        .filter_map(|option| option.card.map(|(_, definition)| definition))
        .collect::<Vec<_>>();

    assert!(
        offered.iter().all(|characteristics| {
            *characteristics
                == ObjectCharacteristics::card(cards::WORLDSPINE_WURM, CardPartId::PRIMARY)
        }),
        "the Bolt is not a green creature card: {offered:?}",
    );
}
