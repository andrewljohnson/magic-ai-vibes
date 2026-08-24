//! Banding's attacking half.
//!
//! CR 702.21: if a creature with banding you control is being blocked by a
//! creature, you divide that creature's combat damage. A blocker only has a
//! division to make when it is blocking more than one attacker, which is what
//! a band produces, so these set that board up and ask who is holding the
//! question.

use super::*;

/// One blocker in front of two attackers. The blocker is big enough that
/// splitting its damage is a real choice, and the attackers band when they
/// can, which is what puts one blocker in front of both.
fn one_blocker_two_attackers(attacker: CardDefinitionId) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    game.step = Step::DeclareAttackers;
    let attackers: Vec<_> = (0..2)
        .map(|index| {
            let permanent = creature(10_000 + index, attacker, PlayerId::One);
            let id = permanent.card.id;
            game.battlefield.push(permanent);
            id
        })
        .collect();
    // Air Elemental for its size: four power is a real division between two
    // recipients. Its flying does not matter to a blocker, and the block
    // below is declared directly rather than chosen.
    let blocker = creature(20_000, cards::AIR_ELEMENTAL, PlayerId::Two);
    let blocker_id = blocker.card.id;
    game.battlefield.push(blocker);

    for id in &attackers {
        game.apply(
            PlayerId::One,
            Action::DeclareAttacker {
                attacker: *id,
                defender: AttackDefender::Player(PlayerId::Two),
            },
        )
        .expect("the attack is legal");
    }
    let banding = Action::BandAttackers {
        first: attackers[0],
        second: attackers[1],
    };
    if game.legal_actions(PlayerId::One).contains(&banding) {
        game.apply(PlayerId::One, banding).expect("just offered");
    }
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("attackers are declared");
    drain_pending(&mut game);

    game.step = Step::DeclareBlockers;
    for id in &attackers {
        game.declare_blocker(blocker_id, *id);
    }
    game.finish_declaring_blockers();
    game.start_combat_damage();
    (game, blocker_id)
}

fn offers_assignment(game: &Game, player: PlayerId) -> bool {
    game.legal_actions(player)
        .iter()
        .any(|action| matches!(action, Action::AssignCombatDamage { .. }))
}

/// The control: no banding anywhere, so the blocker's own controller divides
/// its damage between the two creatures it is holding off.
#[test]
fn without_banding_the_blocking_player_divides_the_blockers_damage() {
    let (game, blocker) = one_blocker_two_attackers(cards::SAVANNAH_LIONS);

    assert!(
        game.pending_combat_assignments.contains(&blocker),
        "a blocker in front of two attackers has a division to make"
    );
    assert_eq!(game.combat_damage_assigner(blocker), PlayerId::Two);
    assert!(offers_assignment(&game, PlayerId::Two));
    assert!(!offers_assignment(&game, PlayerId::One));
}

#[test]
fn a_banding_attacker_takes_the_blockers_assignment() {
    let (game, blocker) = one_blocker_two_attackers(cards::BENALISH_HERO);

    assert_eq!(
        game.combat_damage_assigner(blocker),
        PlayerId::One,
        "the attacking player divides the blocker's damage"
    );
    assert_eq!(game.decision_player(), Some(PlayerId::One));
    assert!(offers_assignment(&game, PlayerId::One));
    assert!(
        !offers_assignment(&game, PlayerId::Two),
        "the blocker's controller no longer chooses"
    );
}

/// The choice is a real one: the attacking player can put the whole blocker's
/// damage on whichever band member they are willing to lose.
#[test]
fn the_attacking_player_can_direct_the_blockers_damage() {
    let (mut game, blocker) = one_blocker_two_attackers(cards::BENALISH_HERO);
    let targets: Vec<_> = game
        .battlefield
        .iter()
        .filter(|permanent| permanent.attacking)
        .map(|permanent| permanent.card.id)
        .collect();

    let chosen = targets[1];
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::AssignCombatDamage {
                attacker,
                assignments,
            } => {
                *attacker == blocker
                    && assignments.iter().all(|assignment| {
                        assignment.recipient == Target::Permanent(chosen) || assignment.amount == 0
                    })
                    && assignments.iter().any(|assignment| assignment.amount > 0)
            }
            _ => false,
        })
        .expect("putting it all on one band member is legal");
    game.apply(PlayerId::One, action)
        .expect("the attacking player may assign");
    drain_pending(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == chosen),
        "the blocker's damage went where the attacking player put it"
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == targets[0]),
        "and the other band member lived"
    );
}

/// A blocker deals its power once, divided among what it blocks, rather than
/// once per attacker. This is what makes the division worth asking about.
#[test]
fn a_blocker_deals_its_power_once_across_both_attackers() {
    let (mut game, _blocker) = one_blocker_two_attackers(cards::HILL_GIANT);
    game.pending_combat_assignments.clear();
    game.deal_combat_damage();

    let survivors = game
        .battlefield
        .iter()
        .filter(|permanent| permanent.attacking)
        .count();
    assert_eq!(
        survivors, 1,
        "four power over two 3/3s kills one and leaves the other; four to each \
         would kill both"
    );
}
