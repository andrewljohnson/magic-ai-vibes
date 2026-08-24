//! Being a blocking creature outlives what was blocked.
//!
//! CR 506.4 lists every way a permanent leaves combat, and "the creature you
//! blocked left" is not on it. So a blocker whose attacker regenerates, or is
//! pulled out of combat, is still a blocking creature: D'Avenant Archer can
//! still shoot it and Righteousness can still pump it. Only the relationship
//! goes, which is why the blocker exchanges no combat damage afterwards.

use super::*;

/// Player one attacks with a Sedge Troll, player two blocks with a Grizzly
/// Bears, and player one holds a D'Avenant Archer to shoot into the block.
fn blocked_combat() -> (Game, GameObjectId, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.turns_started[PlayerId::One.index()] = 5;
    game.step = Step::DeclareBlockers;
    game.attackers_declared = true;

    let archer = creature(10_000, cards::DAVENANT_ARCHER, PlayerId::One);
    let archer_id = archer.card.id;
    game.battlefield.push(archer);

    let mut attacker = creature(10_001, cards::SEDGE_TROLL, PlayerId::One);
    attacker.attacking = true;
    attacker.attack_defender = Some(AttackDefender::Player(PlayerId::Two));
    let attacker_id = attacker.card.id;
    game.battlefield.push(attacker);

    let blocker = creature(10_002, cards::GRIZZLY_BEARS, PlayerId::Two);
    let blocker_id = blocker.card.id;
    game.battlefield.push(blocker);

    game.declare_blocker(blocker_id, attacker_id);
    game.finish_declaring_blockers();
    drain_pending(&mut game);
    (game, archer_id, attacker_id, blocker_id)
}

/// Everything the Archer can shoot: exactly the attacking and blocking
/// creatures.
fn archer_targets(game: &Game, archer: GameObjectId) -> Vec<GameObjectId> {
    let mut found = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } if source == archer => targets
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

fn blocking_relationship(game: &Game, blocker: GameObjectId) -> Vec<GameObjectId> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == blocker)
        .expect("the blocker is on the battlefield")
        .blocking
        .clone()
}

/// The bug: an attacker leaving combat used to take its blocker's status with
/// it, because status was read off the relationship the departure empties.
#[test]
fn a_blocker_stays_blocking_when_its_attacker_leaves_combat() {
    let (mut game, archer, attacker, blocker) = blocked_combat();
    assert_eq!(
        archer_targets(&game, archer),
        vec![attacker, blocker],
        "both sides of the block are attacking or blocking creatures"
    );

    game.remove_permanent_from_combat(attacker);

    assert_eq!(
        archer_targets(&game, archer),
        vec![blocker],
        "the attacker left combat; the creature that blocked it did not"
    );
    assert!(
        blocking_relationship(&game, blocker).is_empty(),
        "the relationship is gone even though the status remains"
    );
}

/// The same seam reached through a real card. A Sedge Troll that regenerates
/// is removed from combat by its own shield.
#[test]
fn a_regenerating_attacker_leaves_its_blocker_blocking() {
    let (mut game, archer, attacker, blocker) = blocked_combat();
    game.add_regeneration_shield(attacker);
    game.damage_target_from(None, Some(Target::Permanent(attacker)), 5);
    game.check_state_based_actions();
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == attacker && !permanent.attacking),
        "the Troll regenerated and left combat"
    );
    assert_eq!(
        archer_targets(&game, archer),
        vec![blocker],
        "its blocker is still a blocking creature"
    );
}

/// The status is the blocker's own, so removing the blocker itself does end
/// it. That is the case CR 506.4 actually names.
#[test]
fn removing_the_blocker_itself_ends_the_status() {
    let (mut game, archer, attacker, blocker) = blocked_combat();

    game.remove_permanent_from_combat(blocker);

    assert_eq!(
        archer_targets(&game, archer),
        vec![attacker],
        "the blocker left combat and stopped being a blocking creature"
    );
}

/// Nothing survives the combat it belongs to.
#[test]
fn the_status_ends_with_combat() {
    let (mut game, archer, attacker, blocker) = blocked_combat();
    game.remove_permanent_from_combat(attacker);
    game.clear_combat();

    assert!(
        archer_targets(&game, archer).is_empty(),
        "combat is over, so nothing is attacking or blocking"
    );
    assert!(blocking_relationship(&game, blocker).is_empty());
}

/// Status is not damage. A blocker with nothing left to fight still deals no
/// combat damage, because damage flows along the relationship.
#[test]
fn a_blocker_whose_attacker_left_deals_no_combat_damage() {
    let (mut game, _, attacker, _) = blocked_combat();
    game.remove_permanent_from_combat(attacker);
    let troll_damage = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == attacker)
        .expect("the Troll is still on the battlefield")
        .damage;
    assert_eq!(troll_damage, 0, "no damage has been dealt yet");

    game.start_combat_damage();
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == attacker && permanent.damage == 0),
        "the creature that had blocked it is no longer blocking it"
    );
}
