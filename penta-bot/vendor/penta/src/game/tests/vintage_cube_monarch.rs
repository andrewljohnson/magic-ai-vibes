//! The monarch (CR 720), and Palace Jailer, which is how it reaches the cube.

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

/// The Jailer takes the crown and jails a creature. Both are enters
/// triggers, so both happen on arrival.
#[test]
fn the_jailer_crowns_you_and_takes_a_creature() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = creature(84_000, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let jailer = card(84_001, cards::PALACE_JAILER, PlayerId::One);
    let jailer_id = jailer.id;
    game.players[0].hand.push(jailer);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == jailer_id))
        .expect("four mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);
    drain_pending(&mut game);

    assert_eq!(game.monarch(), Some(PlayerId::One), "the crown is taken");
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears_id),
        "and the only creature an opponent controlled is gone",
    );
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
        "exiled rather than destroyed",
    );
}

/// "Until an opponent becomes the monarch" is a release, not a deadline:
/// the creature comes back the moment the crown changes hands, and not
/// before.
#[test]
fn losing_the_crown_gives_the_creature_back() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.battlefield
        .push(creature(84_010, cards::GRIZZLY_BEARS, PlayerId::Two));
    let jailer = card(84_011, cards::PALACE_JAILER, PlayerId::One);
    let jailer_id = jailer.id;
    game.players[0].hand.push(jailer);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == jailer_id))
        .expect("four mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);
    drain_pending(&mut game);
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::GRIZZLY_BEARS),
        "jailed to begin with",
    );

    game.set_monarch(PlayerId::Two);
    resolve(&mut game);
    drain_pending(&mut game);

    assert_eq!(game.monarch(), Some(PlayerId::Two));
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::GRIZZLY_BEARS),
        "and the crown changing hands lets the creature out",
    );
}

/// The crown draws its holder a card at the beginning of their end step,
/// and nobody else's.
#[test]
fn the_monarch_draws_at_their_own_end_step() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.set_monarch(PlayerId::One);
    let held = game.players[0].hand.len();

    game.active_player = PlayerId::Two;
    game.handle_end_step();
    assert_eq!(
        game.players[0].hand.len(),
        held,
        "an opponent's end step draws the monarch nothing",
    );

    game.active_player = PlayerId::One;
    game.handle_end_step();
    assert_eq!(
        game.players[0].hand.len(),
        held + 1,
        "and their own draws them a card",
    );
}

/// A creature that gets through to the monarch takes the crown for its
/// controller (CR 720.5).
#[test]
fn combat_damage_to_the_monarch_takes_the_crown() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.set_monarch(PlayerId::One);
    let bears = creature(84_020, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);

    game.damage_target_from_kind(
        Some(bears_id),
        Some(Target::Player(PlayerId::One)),
        2,
        false,
    );
    assert_eq!(
        game.monarch(),
        Some(PlayerId::One),
        "noncombat damage leaves the crown alone",
    );

    game.damage_target_from_kind(Some(bears_id), Some(Target::Player(PlayerId::One)), 2, true);
    assert_eq!(
        game.monarch(),
        Some(PlayerId::Two),
        "and combat damage takes it",
    );
}
