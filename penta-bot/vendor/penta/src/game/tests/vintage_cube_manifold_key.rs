//! Manifold Key: untapping something else, and pushing a creature through.

use super::*;

fn settle(game: &mut Game) {
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

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let key = game
        .put_onto_battlefield(PlayerId::One, cards::MANIFOLD_KEY)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    (game, key)
}

fn activate_at(game: &mut Game, key: GameObjectId, target: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == key
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(target))
            }
            _ => false,
        })
        .expect("the ability is offered at that target");
    game.apply(PlayerId::One, action)
        .expect("the ability activates");
    settle(game);
    drain_pending(game);
}

/// The untap names another artifact, and untaps it.
#[test]
fn it_untaps_another_artifact() {
    let (mut game, key) = staged();
    let lotus = game
        .put_onto_battlefield(PlayerId::One, cards::BLACK_LOTUS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.tap_permanent(lotus);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    activate_at(&mut game, key, lotus);

    assert!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == lotus)
            .is_some_and(|permanent| !permanent.tapped),
        "the Lotus is untapped again",
    );
}

/// "Another" excludes the Key: it cannot untap itself.
#[test]
fn it_cannot_untap_itself() {
    let (mut game, key) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert!(
        !game.legal_actions(PlayerId::One).iter().any(|action| {
            matches!(action, Action::ActivateAbility { source, targets, .. }
                if *source == key
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(key)))
        }),
        "the Key is not another artifact",
    );
}

/// A creature is not an artifact, so the untap does not reach one.
#[test]
fn the_untap_names_artifacts_only() {
    let (mut game, key) = staged();
    let bears = creature(84_000, cards::GRIZZLY_BEARS, PlayerId::One);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    game.tap_permanent(bears_id);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert!(
        !game.legal_actions(PlayerId::One).iter().any(|action| {
            matches!(action, Action::ActivateAbility { source, targets, .. }
                if *source == key
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(bears_id)))
        }),
        "a bear is not an artifact",
    );
}

/// Sets up an attack and reports whether the defender may block it.
fn block_is_offered(unblockable: bool) -> bool {
    let (mut game, key) = staged();
    let mine = creature(84_010, cards::GRIZZLY_BEARS, PlayerId::One);
    let mine_id = mine.card.id;
    game.battlefield.push(mine);
    let theirs = creature(84_011, cards::SERRA_ANGEL, PlayerId::Two);
    let theirs_id = theirs.card.id;
    game.battlefield.push(theirs);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);

    if unblockable {
        activate_at(&mut game, key, mine_id);
    }

    game.turns_started = [2, 1];
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.apply(
        PlayerId::One,
        Action::DeclareAttacker {
            attacker: mine_id,
            defender: AttackDefender::Player(PlayerId::Two),
        },
    )
    .expect("it attacks");
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("the declaration finishes");
    drain_pending(&mut game);
    game.step = Step::DeclareBlockers;
    game.blockers_declared = false;

    game.legal_actions(PlayerId::Two).iter().any(
        |action| matches!(action, Action::DeclareBlocker { blocker, .. } if *blocker == theirs_id),
    )
}

/// The second ability makes an attacker unblockable for the turn -- and
/// without it, the same Angel blocks the same bear happily.
#[test]
fn it_makes_a_creature_unblockable_for_the_turn() {
    assert!(
        block_is_offered(false),
        "the Angel can block an ordinary attacker",
    );
    assert!(
        !block_is_offered(true),
        "and cannot once the Key has spoken",
    );
}
