//! Crucible of Worlds: land drops out of the graveyard.

use super::*;

/// A main phase with a Mountain and a Lightning Bolt in the graveyard.
fn staged(with_crucible: bool) -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    game.players[0]
        .graveyard
        .push(card(87_000, cards::MOUNTAIN, PlayerId::One));
    game.players[0]
        .graveyard
        .push(card(87_001, cards::LIGHTNING_BOLT, PlayerId::One));
    if with_crucible {
        game.put_onto_battlefield(PlayerId::One, cards::CRUCIBLE_OF_WORLDS)
            .expect("cataloged");
        drain_pending(&mut game);
    }
    game
}

fn land_plays(game: &Game) -> Vec<GameObjectId> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::PlayLand { card, .. } => Some(card),
            _ => None,
        })
        .collect()
}

/// Without it a land in the graveyard is not a land drop.
#[test]
fn a_graveyard_land_is_not_playable_on_its_own() {
    let game = staged(false);

    assert!(land_plays(&game).is_empty(), "nothing to play");
}

/// With it the land is offered, and nothing else in the graveyard is.
#[test]
fn the_crucible_offers_the_land_and_only_the_land() {
    let game = staged(true);

    assert_eq!(
        land_plays(&game),
        vec![GameObjectId(87_000)],
        "the Mountain, and not the Bolt",
    );
    assert!(
        !game.legal_actions(PlayerId::One).iter().any(|action| {
            matches!(action, Action::CastSpell { card, .. } if *card == GameObjectId(87_001))
        }),
        "the permission names lands, so the Bolt is still stuck",
    );
}

/// Playing it moves it out of the graveyard and onto the battlefield, and
/// spends the turn's land drop.
#[test]
fn playing_it_takes_the_land_drop() {
    let mut game = staged(true);
    let play = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { .. }))
        .expect("the Mountain is offered");

    game.apply(PlayerId::One, play).expect("it is played");
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::MOUNTAIN),
        "the land is on the battlefield",
    );
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .all(|card| card.definition != cards::MOUNTAIN),
        "and out of the graveyard",
    );
    assert!(
        land_plays(&game).is_empty(),
        "one land drop a turn, wherever it came from",
    );
}

/// The permission is the Crucible's; losing it closes the graveyard again.
#[test]
fn losing_the_crucible_closes_the_graveyard() {
    let mut game = staged(true);
    let crucible = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::CRUCIBLE_OF_WORLDS)
        .map(|permanent| permanent.card.id)
        .expect("it is there");

    game.move_permanents_to_graveyard(&[crucible]);
    drain_pending(&mut game);

    assert!(land_plays(&game).is_empty(), "the permission went with it");
}

/// It is your own graveyard: their lands stay theirs.
#[test]
fn it_does_not_reach_the_opponents_graveyard() {
    let mut game = staged(true);
    game.players[1].graveyard.clear();
    game.players[1]
        .graveyard
        .push(card(87_010, cards::FOREST, PlayerId::Two));

    assert_eq!(
        land_plays(&game),
        vec![GameObjectId(87_000)],
        "only your own Mountain",
    );
}
