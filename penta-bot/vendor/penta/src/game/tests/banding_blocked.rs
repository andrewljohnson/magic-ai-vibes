//! Blocking a band.
//!
//! A band is blocked as a group: one declaration against any member puts the
//! blocker in front of all of them, and it costs the blocker one block rather
//! than one per creature. These drive both halves, because a band that cost
//! three blocks would look correct right up until a second attacker arrived.

use super::*;

/// Player one attacking with `definitions`, banded together wherever the
/// rules allow it, against a player two who has nothing yet. Creatures that
/// cannot band simply attack alongside.
fn banded_attack(definitions: &[CardDefinitionId]) -> (Game, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    game.step = Step::DeclareAttackers;
    let ids: Vec<_> = definitions
        .iter()
        .enumerate()
        .map(|(index, definition)| {
            let permanent = creature(
                10_000 + u32::try_from(index).expect("a small index"),
                *definition,
                PlayerId::One,
            );
            let id = permanent.card.id;
            game.battlefield.push(permanent);
            id
        })
        .collect();
    for id in &ids {
        game.apply(
            PlayerId::One,
            Action::DeclareAttacker {
                attacker: *id,
                defender: AttackDefender::Player(PlayerId::Two),
            },
        )
        .expect("the attack is legal");
    }
    for id in &ids[1..] {
        let action = Action::BandAttackers {
            first: ids[0],
            second: *id,
        };
        if game.legal_actions(PlayerId::One).contains(&action) {
            game.apply(PlayerId::One, action).expect("just offered");
        }
    }
    (game, ids)
}

/// Hands the turn to the blocker with `definitions` on their side.
fn with_blockers(game: &mut Game, definitions: &[CardDefinitionId]) -> Vec<GameObjectId> {
    let ids = definitions
        .iter()
        .enumerate()
        .map(|(index, definition)| {
            let permanent = creature(
                20_000 + u32::try_from(index).expect("a small index"),
                *definition,
                PlayerId::Two,
            );
            let id = permanent.card.id;
            game.battlefield.push(permanent);
            id
        })
        .collect();
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("attackers are declared");
    drain_pending(game);
    game.step = Step::DeclareBlockers;
    game.blockers_declared = false;
    ids
}

fn offered_blocks(game: &Game, blocker: GameObjectId) -> Vec<GameObjectId> {
    game.legal_actions(PlayerId::Two)
        .into_iter()
        .filter_map(|action| match action {
            Action::DeclareBlocker {
                blocker: actual,
                attacker,
            } if actual == blocker => Some(attacker),
            _ => None,
        })
        .collect()
}

fn blocking(game: &Game, blocker: GameObjectId) -> Vec<GameObjectId> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == blocker)
        .expect("still there")
        .blocking
        .clone()
}

#[test]
fn a_band_is_offered_as_one_block_and_taken_as_a_whole() {
    let (mut game, attackers) = banded_attack(&[cards::BENALISH_HERO, cards::MESA_PEGASUS]);
    let blockers = with_blockers(&mut game, &[cards::SERRA_ANGEL]);

    let offers = offered_blocks(&game, blockers[0]);
    assert_eq!(
        offers.len(),
        1,
        "two attackers, but only one band to block: {offers:?}"
    );

    game.apply(
        PlayerId::Two,
        Action::DeclareBlocker {
            blocker: blockers[0],
            attacker: offers[0],
        },
    )
    .expect("the block is legal");

    let mut blocked = blocking(&game, blockers[0]);
    blocked.sort_unstable();
    let mut expected = attackers.clone();
    expected.sort_unstable();
    assert_eq!(blocked, expected, "blocking one member blocks the band");
}

/// The control. Without a band the same two attackers are two separate
/// offers, and blocking one leaves the other unblocked.
#[test]
fn two_unbanded_attackers_are_two_separate_blocks() {
    let (mut game, attackers) = banded_attack(&[cards::SAVANNAH_LIONS, cards::SAVANNAH_LIONS]);
    let blockers = with_blockers(&mut game, &[cards::SERRA_ANGEL]);

    assert_eq!(offered_blocks(&game, blockers[0]).len(), 2);

    game.apply(
        PlayerId::Two,
        Action::DeclareBlocker {
            blocker: blockers[0],
            attacker: attackers[0],
        },
    )
    .expect("the block is legal");

    assert_eq!(blocking(&game, blockers[0]), vec![attackers[0]]);
}

/// A band costs one block, not one per creature, so an ordinary blocker can
/// still take a whole band even though it may only block once.
#[test]
fn blocking_a_band_spends_one_block() {
    let (mut game, _attackers) = banded_attack(&[
        cards::BENALISH_HERO,
        cards::MESA_PEGASUS,
        cards::SAVANNAH_LIONS,
    ]);
    let blockers = with_blockers(&mut game, &[cards::SERRA_ANGEL]);

    let offers = offered_blocks(&game, blockers[0]);
    assert_eq!(offers.len(), 1);
    game.apply(
        PlayerId::Two,
        Action::DeclareBlocker {
            blocker: blockers[0],
            attacker: offers[0],
        },
    )
    .expect("a three-creature band is still one block");

    assert_eq!(blocking(&game, blockers[0]).len(), 3);
}

/// Evasion is checked against every member, because the blocker ends up in
/// front of all of them. A ground creature cannot take a band that contains
/// something flying.
#[test]
fn a_band_containing_a_flier_needs_a_blocker_that_can_take_the_flier() {
    let (mut game, _attackers) = banded_attack(&[cards::BENALISH_HERO, cards::MESA_PEGASUS]);
    let blockers = with_blockers(&mut game, &[cards::SEDGE_TROLL, cards::SERRA_ANGEL]);

    assert!(
        offered_blocks(&game, blockers[0]).is_empty(),
        "the Troll cannot block the Pegasus, so it cannot block its band"
    );
    assert_eq!(
        offered_blocks(&game, blockers[1]).len(),
        1,
        "the Angel flies and can"
    );
}

/// Every member of a blocked band is blocked, so none of them connects.
#[test]
fn no_member_of_a_blocked_band_reaches_the_player() {
    let (mut game, _attackers) = banded_attack(&[cards::BENALISH_HERO, cards::MESA_PEGASUS]);
    let blockers = with_blockers(&mut game, &[cards::SERRA_ANGEL]);
    let offers = offered_blocks(&game, blockers[0]);
    game.apply(
        PlayerId::Two,
        Action::DeclareBlocker {
            blocker: blockers[0],
            attacker: offers[0],
        },
    )
    .expect("the block is legal");

    let before = game.players[PlayerId::Two.index()].life;
    game.deal_combat_damage();

    assert_eq!(
        game.players[PlayerId::Two.index()].life,
        before,
        "the whole band was blocked"
    );
}
