//! Sowing Mycospawn: a colourless card, a `{C}` in its kicker, and a kicker
//! that changes nothing about how the spell resolves.

use super::*;

/// Resolves whatever is on the stack, answering nothing.
fn resolve(game: &mut Game) {
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

/// Puts the Mycospawn in hand and returns its id.
fn mycospawn_in_hand(game: &mut Game, id: u32) -> GameObjectId {
    let card = card(id, cards::SOWING_MYCOSPAWN, PlayerId::One);
    let card_id = card.id;
    game.players[0].hand.push(card);
    card_id
}

/// Devoid: the card is colourless despite its green mana cost.
#[test]
fn the_mycospawn_is_colorless() {
    let game = ready_game();
    let definition = game
        .catalog
        .get(cards::SOWING_MYCOSPAWN)
        .expect("cataloged");
    assert_eq!(
        definition.rules.colors(),
        [false, false, false, false, false],
        "devoid leaves no colour at all",
    );
}

/// The kicker costs `{1}{C}` more, and `{C}` is a requirement generic mana
/// cannot meet: five ordinary mana casts it unkicked and no more.
#[test]
fn the_kicker_needs_real_colorless_mana() {
    let mut game = ready_game();
    game.battlefield.clear();
    let mycospawn = mycospawn_in_hand(&mut game, 88_000);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 5);

    let costs = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card, .. } if *card == mycospawn))
        .count();
    assert_eq!(costs, 1, "six coloured mana still buys only the plain cast");

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    let costs = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card, .. } if *card == mycospawn))
        .count();
    assert_eq!(costs, 2, "one colorless mana opens the kicker");
}

/// Unkicked, the cast trigger fetches a land and nothing else happens -- and
/// the creature still arrives, because the kicker never replaced anything.
#[test]
fn the_unkicked_cast_fetches_a_land_and_leaves_lands_alone() {
    let mut game = ready_game();
    game.battlefield.clear();
    let theirs = creature(88_010, cards::FOREST, PlayerId::Two);
    let theirs_id = theirs.card.id;
    game.battlefield.push(theirs);
    let mycospawn = mycospawn_in_hand(&mut game, 88_011);
    game.players[0].library.clear();
    game.players[0]
        .library
        .push(card(88_012, cards::FOREST, PlayerId::One));
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == mycospawn))
        .expect("four mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SOWING_MYCOSPAWN),
        "the creature still arrives",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::FOREST
                && permanent.controller == PlayerId::One),
        "and the search found a land",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == theirs_id),
        "their land is untouched",
    );
}

/// Kicked, the second cast trigger exiles a land as well -- and the creature
/// still arrives, which is what "the kicker only costs more" means.
#[test]
fn the_kicked_cast_also_exiles_a_land() {
    let mut game = ready_game();
    game.battlefield.clear();
    let theirs = creature(88_020, cards::FOREST, PlayerId::Two);
    let theirs_id = theirs.card.id;
    game.battlefield.push(theirs);
    let mycospawn = mycospawn_in_hand(&mut game, 88_021);
    game.players[0].library.clear();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 5);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == mycospawn && choices.costs().alternative().is_some()
            }
            _ => false,
        })
        .expect("six mana with a colorless one pays the kicker");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);
    drain_pending(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == theirs_id),
        "the kicked trigger takes their land",
    );
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::FOREST),
        "exiled rather than destroyed",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SOWING_MYCOSPAWN),
        "and the creature still arrives",
    );
}
