//! Populate.
//!
//! Choose a creature token you control, then copy it. The choice is not a
//! target, so nothing is rechecked; and a board with no creature tokens is
//! not a failure, it simply does nothing.

use super::*;
use crate::ImplementationStatus;

fn tokens_of(game: &Game, definition: CardDefinitionId) -> usize {
    game.battlefield
        .iter()
        .filter(|permanent| permanent.card.definition == definition)
        .count()
}

/// Casts Rootborn Defenses, which is populate with nothing else in front of
/// it, and answers whatever choice it asks.
fn populate_with(game: &mut Game) {
    let spell = card(10_000, cards::ROOTBORN_DEFENSES, PlayerId::One);
    let spell_id = spell.id;
    game.players[PlayerId::One.index()].hand.push(spell);
    game.players[PlayerId::One.index()].mana_pool.white = 1;
    game.players[PlayerId::One.index()].mana_pool.colorless = 2;

    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell_id))
        .expect("it can be cast");
    game.apply(PlayerId::One, action)
        .expect("the spell is cast");
    drain_pending(game);
}

#[test]
fn populate_copies_a_creature_token_you_control() {
    let mut game = ready_game();
    let token = creature(10_001, cards::SOLDIER_TOKEN_1_1_WHITE, PlayerId::One);
    game.battlefield.push(token);

    populate_with(&mut game);

    assert_eq!(
        tokens_of(&game, cards::SOLDIER_TOKEN_1_1_WHITE),
        2,
        "the chosen token was copied"
    );
}

/// A nontoken creature is not a candidate, however big it is.
#[test]
fn a_nontoken_creature_is_not_copied() {
    let mut game = ready_game();
    let troll = creature(10_001, cards::SEDGE_TROLL, PlayerId::One);
    game.battlefield.push(troll);

    populate_with(&mut game);

    assert_eq!(
        tokens_of(&game, cards::SEDGE_TROLL),
        1,
        "a printed creature is not a token to copy"
    );
}

/// Nor is a token an opponent controls.
#[test]
fn an_opponents_token_is_not_copied() {
    let mut game = ready_game();
    let theirs = creature(10_001, cards::SOLDIER_TOKEN_1_1_WHITE, PlayerId::Two);
    game.battlefield.push(theirs);

    populate_with(&mut game);

    assert_eq!(tokens_of(&game, cards::SOLDIER_TOKEN_1_1_WHITE), 1);
}

/// With nothing to copy the spell still resolves; the rest of its text has to
/// happen either way.
#[test]
fn nothing_to_copy_is_not_a_failure() {
    let mut game = ready_game();
    let troll = creature(10_001, cards::SEDGE_TROLL, PlayerId::One);
    let troll_id = troll.card.id;
    game.battlefield.push(troll);

    populate_with(&mut game);

    let troll = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == troll_id)
        .expect("still there");
    assert!(
        game.permanent_has_executable_keyword(troll, KeywordAbility::Indestructible),
        "the indestructible half still happened"
    );
}

/// A card that makes a token and then populates copies the one it just made,
/// which is the ordering the word "then" carries.
#[test]
fn making_a_token_first_gives_populate_something_to_copy() {
    let mut game = ready_game();
    let spell = card(10_000, cards::COURSERS_ACCORD, PlayerId::One);
    let spell_id = spell.id;
    game.players[PlayerId::One.index()].hand.push(spell);
    game.players[PlayerId::One.index()].mana_pool.green = 1;
    game.players[PlayerId::One.index()].mana_pool.white = 1;
    game.players[PlayerId::One.index()].mana_pool.colorless = 4;

    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell_id))
        .expect("it can be cast");
    game.apply(PlayerId::One, action)
        .expect("the spell is cast");
    drain_pending(&mut game);

    assert_eq!(
        tokens_of(&game, cards::CENTAUR_TOKEN_3_3_GREEN),
        2,
        "one made, then one copied from it"
    );
}

#[test]
fn every_populate_identity_reports_complete_coverage() {
    let catalog = poc::catalog().expect("catalog builds");
    for definition in [
        cards::EYES_IN_THE_SKIES,
        cards::ROOTBORN_DEFENSES,
        cards::GROWING_RANKS,
        cards::TROSTANIS_JUDGMENT,
        cards::HORNCALLERS_CHANT,
        cards::COURSERS_ACCORD,
    ] {
        let card = catalog.get(definition).expect("the card is cataloged");
        assert_eq!(
            card.rules.implementation_status(),
            ImplementationStatus::Complete,
            "{} should be fully executable",
            card.name,
        );
    }
}
