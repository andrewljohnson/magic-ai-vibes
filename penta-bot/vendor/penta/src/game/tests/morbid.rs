//! Morbid.
//!
//! An entry replacement that only applies if a creature died this turn. The
//! condition is read as the permanent enters rather than when its spell was
//! cast, which is what lets a creature dying in response turn it on.

use super::*;
use crate::ImplementationStatus;

fn cast_boar(game: &mut Game) -> GameObjectId {
    let boar = card(10_000, cards::FESTERHIDE_BOAR, PlayerId::One);
    let boar_id = boar.id;
    game.players[PlayerId::One.index()].hand.push(boar);
    game.players[PlayerId::One.index()].mana_pool.green = 1;
    game.players[PlayerId::One.index()].mana_pool.colorless = 3;

    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == boar_id))
        .expect("the Boar is castable");
    game.apply(PlayerId::One, action)
        .expect("the spell is cast");
    drain_pending(game);

    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::FESTERHIDE_BOAR)
        .expect("the Boar entered")
        .card
        .id
}

fn counters(game: &Game, id: GameObjectId) -> u16 {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("on the battlefield")
        .counters(CounterKind::PlusOnePlusOne)
}

#[test]
fn without_a_death_it_enters_as_printed() {
    let mut game = ready_game();
    let boar = cast_boar(&mut game);

    assert_eq!(counters(&game, boar), 0, "nothing has died this turn");
    let boar = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == boar)
        .expect("there");
    assert_eq!((game.power(boar), game.toughness(boar)), (Some(3), Some(3)));
}

#[test]
fn a_death_this_turn_adds_two_counters() {
    let mut game = ready_game();
    let doomed = creature(10_001, cards::SEDGE_TROLL, PlayerId::One);
    let doomed_id = doomed.card.id;
    game.battlefield.push(doomed);
    game.destroy_permanent(doomed_id);
    drain_pending(&mut game);

    let boar = cast_boar(&mut game);
    assert_eq!(counters(&game, boar), 2, "morbid is on");
    let boar = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == boar)
        .expect("there");
    assert_eq!((game.power(boar), game.toughness(boar)), (Some(5), Some(5)));
}

#[test]
fn every_morbid_identity_reports_complete_coverage() {
    let catalog = poc::catalog().expect("catalog builds");
    for definition in [cards::FESTERHIDE_BOAR, cards::SOMBERWALD_SPIDER] {
        let card = catalog.get(definition).expect("the card is cataloged");
        assert_eq!(
            card.rules.implementation_status(),
            ImplementationStatus::Complete,
            "{} should be fully executable",
            card.name,
        );
    }
}
