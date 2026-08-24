//! Fallen Shinobi: ninjutsu, and playing somebody else's cards for free.

use super::*;

/// Puts `attacker` into the declare-blockers window, attacking player two
/// and unblocked, which is where ninjutsu lives.
fn attacking_unblocked(game: &mut Game, attacker: GameObjectId) {
    game.step = Step::DeclareAttackers;
    game.attackers_declared = true;
    game.priority = PlayerId::One;
    for permanent in &mut game.battlefield {
        if permanent.card.id == attacker {
            permanent.attacking = true;
            permanent.tapped = true;
            permanent.attack_defender = Some(AttackDefender::Player(PlayerId::Two));
        }
    }
}

/// The one ninjutsu activation on offer for the card in hand.
fn ninjutsu_action(game: &Game, ninja: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == ninja))
}

/// Ninjutsu swaps the unblocked attacker for the ninja, which arrives tapped
/// and attacking the same player.
#[test]
fn ninjutsu_swaps_an_unblocked_attacker_for_the_ninja() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = creature(90_000, cards::GRIZZLY_BEARS, PlayerId::One);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let shinobi = card(90_001, cards::FALLEN_SHINOBI, PlayerId::One);
    let shinobi_id = shinobi.id;
    game.players[0].hand.push(shinobi);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    attacking_unblocked(&mut game, bears_id);

    let action = ninjutsu_action(&game, shinobi_id).expect("ninjutsu is offered");
    game.apply(PlayerId::One, action).expect("it is activated");
    pass_until_decision(&mut game);
    drain_pending(&mut game);

    assert!(
        game.players[0]
            .hand
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
        "the attacker went back to hand",
    );
    let ninja = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::FALLEN_SHINOBI)
        .expect("the ninja arrived");
    assert!(ninja.tapped, "tapped");
    assert!(ninja.attacking, "and attacking");
    assert_eq!(
        ninja.attack_defender,
        Some(AttackDefender::Player(PlayerId::Two)),
        "the same player the returned creature was attacking",
    );
}

/// The window is exactly between the two declarations: nothing before
/// attackers are declared, and nothing once blockers are.
#[test]
fn ninjutsu_only_opens_between_the_two_declarations() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = creature(90_010, cards::GRIZZLY_BEARS, PlayerId::One);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let shinobi = card(90_011, cards::FALLEN_SHINOBI, PlayerId::One);
    let shinobi_id = shinobi.id;
    game.players[0].hand.push(shinobi);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);

    assert!(
        ninjutsu_action(&game, shinobi_id).is_none(),
        "no attackers yet, so nothing to return",
    );
    attacking_unblocked(&mut game, bears_id);
    assert!(
        ninjutsu_action(&game, shinobi_id).is_some(),
        "the window is open between the declarations",
    );
    game.step = Step::DeclareBlockers;
    assert!(
        ninjutsu_action(&game, shinobi_id).is_none(),
        "and shut once the step advances to blocking",
    );
}

/// Connecting exiles the top two of the defending player's library and lets
/// their attacker play them, free, this turn.
#[test]
fn the_shinobi_plays_the_two_cards_it_took_for_free() {
    let mut game = ready_game();
    game.battlefield.clear();
    let shinobi = creature(90_020, cards::FALLEN_SHINOBI, PlayerId::One);
    let shinobi_id = shinobi.card.id;
    game.battlefield.push(shinobi);
    game.players[1].library.clear();
    // Serra Angel costs five; the point is that none of it is paid.
    game.players[1]
        .library
        .push(card(90_021, cards::GRIZZLY_BEARS, PlayerId::Two));
    game.players[1]
        .library
        .push(card(90_022, cards::SERRA_ANGEL, PlayerId::Two));

    game.damage_target_from_kind(
        Some(shinobi_id),
        Some(Target::Player(PlayerId::Two)),
        5,
        true,
    );
    pass_until_decision(&mut game);
    drain_pending(&mut game);

    assert_eq!(
        game.players[1].exile.len(),
        2,
        "two cards left their library for exile",
    );
    let angel = game.players[1]
        .exile
        .iter()
        .find(|card| card.definition == cards::SERRA_ANGEL)
        .expect("the Angel is one of them");
    let angel_id = angel.id;

    // No mana at all, and it is still castable.
    assert!(
        game.legal_actions(PlayerId::One)
            .into_iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if card == angel_id)),
        "a five-mana creature is free from exile",
    );
    assert!(
        game.legal_actions(PlayerId::Two)
            .into_iter()
            .all(|action| !matches!(action, Action::CastSpell { card, .. } if card == angel_id)),
        "and its owner may not play it",
    );
}

/// The permission is for this turn only.
#[test]
fn the_free_plays_lapse_at_end_of_turn() {
    let mut game = ready_game();
    game.battlefield.clear();
    let shinobi = creature(90_030, cards::FALLEN_SHINOBI, PlayerId::One);
    let shinobi_id = shinobi.card.id;
    game.battlefield.push(shinobi);
    game.players[1].library.clear();
    game.players[1]
        .library
        .push(card(90_031, cards::GRIZZLY_BEARS, PlayerId::Two));

    game.damage_target_from_kind(
        Some(shinobi_id),
        Some(Target::Player(PlayerId::Two)),
        5,
        true,
    );
    pass_until_decision(&mut game);
    drain_pending(&mut game);
    let taken = game.players[1].exile.first().expect("one card taken").id;
    assert!(
        game.legal_actions(PlayerId::One)
            .into_iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if card == taken)),
        "playable while the turn lasts",
    );

    game.active_player = PlayerId::Two;
    assert!(
        game.legal_actions(PlayerId::One)
            .into_iter()
            .all(|action| !matches!(action, Action::CastSpell { card, .. } if card == taken)),
        "and gone once the turn is over",
    );
}
