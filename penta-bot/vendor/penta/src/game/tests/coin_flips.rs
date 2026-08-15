//! Printed coin flips.
//!
//! The randomiser has been seeded and replay-stable since Chaos Orb used it;
//! a coin is that with an even chance and two branches. What these check is
//! that both branches are reachable and that each does its own thing, since a
//! flip whose loss branch never fires would look correct in casual play.

use super::*;
use crate::ImplementationStatus;

/// Drives the same activation under many seeds and returns how the results
/// split. A flip that always went one way would show up as a single outcome.
fn outcomes(mut play: impl FnMut(u64) -> bool) -> (usize, usize) {
    let mut won = 0;
    let mut lost = 0;
    for seed in 0..40 {
        if play(seed) {
            won += 1;
        } else {
            lost += 1;
        }
    }
    (won, lost)
}

#[test]
fn bottle_of_suleiman_reaches_both_branches() {
    let (won, lost) = outcomes(|seed| {
        let mut game = ready_game_with_seed(seed);
        game.turns_started[PlayerId::One.index()] = 1;
        let bottle = creature(10_000, cards::BOTTLE_OF_SULEIMAN, PlayerId::One);
        let bottle_id = bottle.card.id;
        game.battlefield.push(bottle);
        game.players[PlayerId::One.index()].mana_pool.colorless = 1;

        let action = game
            .legal_actions(PlayerId::One)
            .into_iter()
            .find(|action| {
                matches!(action, Action::ActivateAbility { source, .. } if *source == bottle_id)
            })
            .expect("the Bottle offers its ability");
        game.apply(PlayerId::One, action)
            .expect("the ability activates");
        drain_pending(&mut game);

        let djinn = game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::DJINN_TOKEN_5_5_COLORLESS);
        let damaged = game.players[PlayerId::One.index()].life < i16::from(rules::STARTING_LIFE);
        // Exactly one branch happens, whichever it was.
        assert_ne!(djinn, damaged, "one branch, not both and not neither");
        djinn
    });

    assert!(won > 0, "the Djinn branch is reachable");
    assert!(lost > 0, "and so is the five damage");
}

#[test]
fn orcish_captain_pumps_or_shrinks_the_same_orc() {
    let (won, lost) = outcomes(|seed| {
        let mut game = ready_game_with_seed(seed);
        let captain = creature(10_000, cards::ORCISH_CAPTAIN, PlayerId::One);
        let captain_id = captain.card.id;
        game.battlefield.push(captain);
        let orc = creature(10_001, cards::ORCISH_ARTILLERY, PlayerId::One);
        let orc_id = orc.card.id;
        game.battlefield.push(orc);
        game.players[PlayerId::One.index()].mana_pool.colorless = 1;

        let action = game
            .legal_actions(PlayerId::One)
            .into_iter()
            .find(|action| match action {
                Action::ActivateAbility {
                    source, targets, ..
                } => {
                    *source == captain_id
                        && targets
                            .iter()
                            .flat_map(crate::casting::TargetSelection::targets)
                            .any(|target| *target == Target::Permanent(orc_id))
                }
                _ => false,
            })
            .expect("the Captain can point at the Orc");
        game.apply(PlayerId::One, action)
            .expect("the ability activates");
        drain_pending(&mut game);

        let pumped = game
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == orc_id)
            .expect("the Orc is still there");
        let power = game.power(pumped).expect("it is a creature");
        power > 1
    });

    assert!(won > 0, "the winning branch is reachable");
    assert!(lost > 0, "and so is the losing one");
}

/// Mijae Djinn attacks and then may take itself out of combat. The losing
/// branch is the interesting one: a 6/3 that stops attacking and taps.
#[test]
fn mijae_djinn_either_attacks_or_takes_itself_out() {
    let (won, lost) = outcomes(|seed| {
        let mut game = ready_game_with_seed(seed);
        let djinn = creature(10_000, cards::MIJAE_DJINN, PlayerId::One);
        let djinn_id = djinn.card.id;
        game.battlefield.push(djinn);
        game.step = Step::DeclareAttackers;
        game.active_player = PlayerId::One;
        game.priority = PlayerId::One;

        game.apply(
            PlayerId::One,
            Action::DeclareAttacker {
                attacker: djinn_id,
                defender: AttackDefender::Player(PlayerId::Two),
            },
        )
        .expect("a 6/3 can attack");
        game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
            .expect("attackers are declared");
        drain_pending(&mut game);

        let djinn = game
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == djinn_id)
            .expect("still there");
        // Declaring it tapped it either way; what the coin decides is
        // whether it is still in the attack.
        assert!(djinn.tapped);
        let attacking = djinn.attacking;

        let before = game.players[PlayerId::Two.index()].life;
        game.deal_combat_damage();
        let dealt = before - game.players[PlayerId::Two.index()].life;
        assert_eq!(
            dealt,
            if attacking { 6 } else { 0 },
            "a Djinn out of combat deals nothing"
        );
        attacking
    });

    assert!(won > 0, "it can stay in the attack");
    assert!(lost > 0, "and it can take itself out");
}

/// Removing a blocker from combat frees the attacker it was blocking from
/// having a blocker, which is what CR 506.4 asks of the removal itself.
#[test]
fn removing_a_blocker_from_combat_clears_the_blocking_relationship() {
    let mut game = ready_game();
    let mut attacker = creature(10_000, cards::SERRA_ANGEL, PlayerId::One);
    attacker.attacking = true;
    let attacker_id = attacker.card.id;
    game.battlefield.push(attacker);
    let mut blocker = creature(10_001, cards::SEDGE_TROLL, PlayerId::Two);
    blocker.blocking = Some(attacker_id);
    let blocker_id = blocker.card.id;
    game.battlefield.push(blocker);

    game.remove_permanent_from_combat(blocker_id);

    let blocker = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == blocker_id)
        .expect("still there");
    assert_eq!(blocker.blocking, None);

    game.deal_combat_damage();
    let blocker = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == blocker_id)
        .expect("a creature out of combat takes nothing");
    assert_eq!(blocker.damage, 0);
}

#[test]
fn both_identities_report_complete_coverage() {
    let catalog = poc::catalog().expect("catalog builds");
    for definition in [
        cards::ORCISH_CAPTAIN,
        cards::BOTTLE_OF_SULEIMAN,
        cards::MIJAE_DJINN,
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
