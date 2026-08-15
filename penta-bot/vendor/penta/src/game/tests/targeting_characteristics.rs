//! Target legality reads the real numbers, not the recursion-safe ones.
//!
//! Trigger capture and static resolution share a characteristics view that
//! deliberately leaves continuous static effects out: it is used *while* those
//! effects are being resolved, so asking for a value that depends on them
//! would re-enter the computation. Target legality is asked from outside that
//! resolution and so gets the real values, which is what these pin.

use super::*;

fn legal_targets(game: &Game, source: GameObjectId) -> Vec<GameObjectId> {
    let mut found = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateAbility {
                source: actual,
                targets,
                ..
            } if actual == source => targets
                .iter()
                .flat_map(crate::casting::TargetSelection::targets)
                .find_map(|target| match target {
                    Target::Permanent(id) => Some(*id),
                    _ => None,
                }),
            _ => None,
        })
        .collect::<Vec<_>>();
    found.sort_unstable();
    found
}

/// Pendelhaven targets a 1/1. A Crusade makes every white creature 2/2, so a
/// white 1/1 stops being a legal target while the Crusade is out -- which is
/// the whole point of the clause it prints.
#[test]
fn a_statically_pumped_creature_is_no_longer_a_one_one() {
    let mut game = ready_game();
    let pendelhaven = creature(10_000, cards::PENDELHAVEN, PlayerId::One);
    let pendelhaven_id = pendelhaven.card.id;
    game.battlefield.push(pendelhaven);
    // A white 1/1, so the Crusade below reaches it.
    let javelineers = creature(10_001, cards::ICATIAN_JAVELINEERS, PlayerId::One);
    let javelineers_id = javelineers.card.id;
    game.battlefield.push(javelineers);
    game.turns_started[PlayerId::One.index()] = 1;

    assert_eq!(
        legal_targets(&game, pendelhaven_id),
        vec![javelineers_id],
        "a printed 1/1 is a legal target"
    );

    game.battlefield
        .push(creature(10_002, cards::CRUSADE, PlayerId::One));

    assert!(
        legal_targets(&game, pendelhaven_id).is_empty(),
        "the Crusade made it a 2/2, so it is not a 1/1 any more"
    );
}

/// The same seam in the other direction. "Power 2 or less" is a negation, so
/// reading the smaller number made the clause too permissive: a creature a
/// Crusade had already pushed past the ceiling still qualified.
#[test]
fn a_statically_pumped_creature_leaves_a_power_ceiling() {
    let mut game = ready_game();
    let warriors = creature(10_000, cards::DWARVEN_WARRIORS, PlayerId::One);
    let warriors_id = warriors.card.id;
    game.battlefield.push(warriors);
    game.turns_started[PlayerId::One.index()] = 1;
    let bear = creature(10_001, cards::SAVANNAH_LIONS, PlayerId::One);
    let bear_id = bear.card.id;
    game.battlefield.push(bear);

    assert!(
        legal_targets(&game, warriors_id).contains(&bear_id),
        "a 2/1 is within the ceiling"
    );

    // Two Crusades put it at 4/3, well past "power 2 or less".
    game.battlefield
        .push(creature(10_002, cards::CRUSADE, PlayerId::One));
    game.battlefield
        .push(creature(10_003, cards::CRUSADE, PlayerId::One));

    assert!(
        !legal_targets(&game, warriors_id).contains(&bear_id),
        "the statics pushed it past the ceiling"
    );
}
