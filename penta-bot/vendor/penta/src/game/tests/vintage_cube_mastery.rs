//! Baleful Mastery: two prices for one exile, and the rider on the cheap one.

use super::*;

fn staged() -> (Game, CardInstanceId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let victim = creature(89_000, cards::SERRA_ANGEL, PlayerId::Two);
    let victim_id = victim.card.id;
    game.battlefield.push(victim);
    let mastery = card(89_001, cards::BALEFUL_MASTERY, PlayerId::One);
    let mastery_id = mastery.id;
    game.players[0].hand.push(mastery);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);
    (game, mastery_id, victim_id)
}

fn cast_mastery(game: &mut Game, mastery: CardInstanceId, discounted: bool) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == mastery && choices.costs().alternative().is_some() == discounted
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("a cast with discounted={discounted} is offered"));
    game.apply(PlayerId::One, cast).expect("it is cast");
    pass_priority_pair(game);
    drain_pending(game);
}

/// Cast for its printed cost it exiles and gives nothing back.
#[test]
fn the_full_price_exiles_and_pays_nothing() {
    let (mut game, mastery, victim) = staged();
    let their_hand = game.players[1].hand.len();

    cast_mastery(&mut game, mastery, false);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != victim),
        "the creature is gone",
    );
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL),
        "exiled rather than destroyed",
    );
    assert_eq!(
        game.players[1].hand.len(),
        their_hand,
        "and they drew nothing",
    );
}

/// Cast for the discount it still exiles, and the opponent draws.
#[test]
fn the_discount_exiles_and_hands_them_a_card() {
    let (mut game, mastery, victim) = staged();
    let their_hand = game.players[1].hand.len();

    cast_mastery(&mut game, mastery, true);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != victim),
        "the creature is gone either way",
    );
    assert_eq!(
        game.players[1].hand.len(),
        their_hand + 1,
        "and the discount cost them a card",
    );
    assert_eq!(
        game.players[0].mana_pool.total(),
        2,
        "two of the four mana are left over",
    );
}

/// It answers a planeswalker as readily as a creature.
#[test]
fn it_exiles_a_planeswalker_too() {
    let mut game = ready_game();
    game.battlefield.clear();
    let walker = game
        .put_onto_battlefield(PlayerId::Two, cards::JACE_MEMORY_ADEPT)
        .expect("cataloged");
    drain_pending(&mut game);
    let mastery = card(89_010, cards::BALEFUL_MASTERY, PlayerId::One);
    let mastery_id = mastery.id;
    game.players[0].hand.push(mastery);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);

    cast_mastery(&mut game, mastery_id, false);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != walker),
        "the planeswalker is gone",
    );
}
