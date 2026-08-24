//! Baleful Strix: two mana that replaces itself and then eats whatever
//! attacks into it.

use super::*;

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::One.index()].library.clear();
    game.players[PlayerId::One.index()].library.push(card(
        86_000,
        cards::GIANT_GROWTH,
        PlayerId::One,
    ));
    let strix = game
        .put_onto_battlefield(PlayerId::One, cards::BALEFUL_STRIX)
        .expect("cataloged");
    drain_pending(&mut game);
    (game, strix)
}

/// Flying and deathtouch, and a card on the way in.
#[test]
fn it_flies_touches_and_draws() {
    let (game, strix) = staged();
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == strix)
        .expect("it is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Flying));
    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Deathtouch));
    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GIANT_GROWTH],
        "entering draws one",
    );
}

/// It is an artifact as well as a creature, which is what makes it a
/// target for artifact removal and food for artifact synergies.
#[test]
fn it_is_an_artifact_creature() {
    let (game, strix) = staged();
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == strix)
        .expect("it is there");

    assert!(
        game.permanent_types(permanent)
            .is_some_and(|types| types.contains(CardType::Artifact)),
    );
    assert!(
        game.permanent_types(permanent)
            .is_some_and(|types| types.contains(CardType::Creature)),
    );
    assert_eq!(
        (game.power(permanent), game.toughness(permanent)),
        (Some(1), Some(1))
    );
}

/// A 1/1 with deathtouch kills whatever it damages, however large.
#[test]
fn its_damage_is_lethal_whatever_it_hits() {
    let (mut game, strix) = staged();
    let angel = creature(86_100, cards::SERRA_ANGEL, PlayerId::Two);
    let angel_id = angel.card.id;
    game.battlefield.push(angel);

    game.damage_target_from(Some(strix), Some(Target::Permanent(angel_id)), 1);
    game.check_state_based_actions();

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != angel_id),
        "one point from a source with deathtouch is lethal",
    );
}
