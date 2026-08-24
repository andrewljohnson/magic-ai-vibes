//! Bitter Triumph: unconditional removal for two mana, and one cost with
//! two ways to pay it.

use super::*;

/// Player One holding a Bitter Triumph, with a creature to point it at.
fn staged(hand_size: usize, life: i16) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for index in 0..hand_size {
        game.players[0].hand.push(card(
            96_000 + u32::try_from(index).expect("a small hand"),
            cards::MOUNTAIN,
            PlayerId::One,
        ));
    }
    let triumph = card(96_100, cards::BITTER_TRIUMPH, PlayerId::One);
    let triumph_id = triumph.id;
    game.players[0].hand.push(triumph);
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.players[0].life = life;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, triumph_id, bears)
}

/// Every way the Triumph is currently castable, by what it spends.
fn casts(game: &Game, triumph: GameObjectId) -> Vec<Vec<GameObjectId>> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::CastSpell {
                card, sacrifices, ..
            } if card == triumph => Some(sacrifices),
            _ => None,
        })
        .collect()
}

/// With cards in hand and life to spare, both ways are on offer.
#[test]
fn both_ways_to_pay_are_offered() {
    let (game, triumph, _bears) = staged(2, 20);
    let ways = casts(&game, triumph);

    assert!(
        ways.iter().any(|spent| spent.len() == 1),
        "discarding a card is one way: {ways:?}",
    );
    assert!(
        ways.iter().any(Vec::is_empty),
        "paying the life is the other: {ways:?}",
    );
}

/// Discarding pays it, and the life is untouched.
#[test]
fn discarding_a_card_pays_it() {
    let (mut game, triumph, bears) = staged(2, 20);
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, sacrifices, .. }
                if *card == triumph && sacrifices.len() == 1)
        })
        .expect("a card in hand pays for it");

    game.apply(PlayerId::One, action)
        .expect("the cast is legal");
    drain_pending(&mut game);

    assert_eq!(game.players[0].life, 20, "the life was not spent");
    assert_eq!(game.players[0].graveyard.len(), 2, "the card and the spell");
    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != bears),
        "and the bear is destroyed",
    );
}

/// Paying the life pays it, and the hand is untouched.
#[test]
fn paying_three_life_pays_it() {
    let (mut game, triumph, bears) = staged(2, 20);
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, sacrifices, .. }
                if *card == triumph && sacrifices.is_empty())
        })
        .expect("three life pays for it");

    game.apply(PlayerId::One, action)
        .expect("the cast is legal");
    drain_pending(&mut game);

    assert_eq!(game.players[0].life, 17);
    assert_eq!(game.players[0].hand.len(), 2, "the hand kept its cards");
    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != bears),
        "and the bear is destroyed",
    );
}

/// An empty hand leaves only the life.
#[test]
fn an_empty_hand_leaves_only_the_life() {
    let (game, triumph, _bears) = staged(0, 20);
    let ways = casts(&game, triumph);

    assert_eq!(ways.len(), 1, "one way, and it spends nothing: {ways:?}");
    assert!(ways[0].is_empty());
}

/// Two life is not three, so that way is closed.
#[test]
fn too_little_life_leaves_only_the_discard() {
    let (game, triumph, _bears) = staged(2, 2);
    let ways = casts(&game, triumph);

    assert!(
        ways.iter().all(|spent| spent.len() == 1),
        "there is no life to pay with: {ways:?}",
    );
}

/// Paying down to exactly zero is legal (CR 118.4), and loses the game to
/// the state-based action rather than to the cost.
#[test]
fn exactly_three_life_is_still_payable() {
    let (game, triumph, _bears) = staged(0, 3);
    let ways = casts(&game, triumph);

    assert_eq!(ways.len(), 1, "three life is enough for three life");
}

/// Neither way, no spell.
#[test]
fn an_empty_hand_and_no_life_cannot_cast_it() {
    let (game, triumph, _bears) = staged(0, 2);

    assert!(casts(&game, triumph).is_empty());
}
