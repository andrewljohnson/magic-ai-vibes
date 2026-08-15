//! The blocker's own prohibition.
//!
//! The vocabulary had only the attacker-side restriction, so "this creature
//! can't block" and "target creature can't block this turn" had no shape at
//! all. These drive the prohibition through the blocker list a seat is
//! offered, both as a printed static and as a rider a spell hands out.

use super::*;
use crate::ImplementationStatus;

fn combat(defender: CardDefinitionId) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.step = Step::DeclareBlockers;
    let mut attacker = creature(10_000, cards::SEDGE_TROLL, PlayerId::One);
    attacker.attacking = true;
    attacker.attack_defender = Some(AttackDefender::Player(PlayerId::Two));
    let attacker_id = attacker.card.id;
    game.battlefield.push(attacker);
    let blocker = creature(10_001, defender, PlayerId::Two);
    let blocker_id = blocker.card.id;
    game.battlefield.push(blocker);
    (game, attacker_id, blocker_id)
}

fn can_block(game: &Game, blocker: GameObjectId) -> bool {
    game.legal_actions(PlayerId::Two).iter().any(
        |action| matches!(action, Action::DeclareBlocker { blocker: actual, .. } if *actual == blocker),
    )
}

#[test]
fn a_creature_that_cannot_block_is_not_offered_as_a_blocker() {
    let (game, _attacker_id, blocker_id) = combat(cards::VAMPIRE_INTERLOPER);
    assert!(
        !can_block(&game, blocker_id),
        "the printed restriction keeps it out of the blocker list"
    );
}

/// The same board with an ordinary creature, so the test above is measuring
/// the restriction rather than something else about the setup.
#[test]
fn an_ordinary_creature_is_offered() {
    let (game, _attacker_id, blocker_id) = combat(cards::SAVANNAH_LIONS);
    assert!(can_block(&game, blocker_id));
}

/// A spell hands the same prohibition out for the turn, which is the shape
/// the runtime boundary had to be widened for: every other blocking
/// restriction is continuous.
#[test]
fn a_spell_can_hand_out_the_prohibition_for_the_turn() {
    let (mut game, _attacker_id, blocker_id) = combat(cards::SAVANNAH_LIONS);
    assert!(can_block(&game, blocker_id));

    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    let clutches = card(10_002, cards::NIGHTBIRDS_CLUTCHES, PlayerId::One);
    let clutches_id = clutches.id;
    game.players[PlayerId::One.index()].hand.push(clutches);
    game.players[PlayerId::One.index()].mana_pool.red = 1;
    game.players[PlayerId::One.index()].mana_pool.colorless = 1;

    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == clutches_id
                    && choices
                        .iter_targets()
                        .any(|target| *target == Target::Permanent(blocker_id))
            }
            _ => false,
        })
        .expect("the Clutches can name that creature");
    game.apply(PlayerId::One, action)
        .expect("the spell is cast");
    drain_pending(&mut game);

    game.step = Step::DeclareBlockers;
    assert!(
        !can_block(&game, blocker_id),
        "the spell took its blocking away for the turn"
    );
}

#[test]
fn every_cannot_block_identity_reports_complete_coverage() {
    let catalog = poc::catalog().expect("catalog builds");
    for definition in [
        cards::SIGHTLESS_GHOUL,
        cards::MARKOV_WARLORD,
        cards::VAMPIRE_INTERLOPER,
        cards::CROSSWAY_VAMPIRE,
        cards::NIGHTBIRDS_CLUTCHES,
        cards::FIREFIST_STRIKER,
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

/// The attacker's side, printed as a static rather than handed out for a
/// turn. Both forms had to exist for the same reason the blocker's side did:
/// the turn-scoped one is a resolving rider and this one holds while its
/// source does.
mod cannot_be_blocked {
    use super::*;

    fn attacking(definition: CardDefinitionId) -> (Game, GameObjectId, GameObjectId) {
        let mut game = ready_game();
        game.step = Step::DeclareBlockers;
        game.attackers_declared = true;
        let mut attacker = creature(10_000, definition, PlayerId::One);
        attacker.attacking = true;
        attacker.attack_defender = Some(AttackDefender::Player(PlayerId::Two));
        let attacker_id = attacker.card.id;
        game.battlefield.push(attacker);
        let blocker = creature(10_001, cards::SEDGE_TROLL, PlayerId::Two);
        let blocker_id = blocker.card.id;
        game.battlefield.push(blocker);
        (game, attacker_id, blocker_id)
    }

    fn offers_block(game: &Game, attacker: GameObjectId) -> bool {
        game.legal_actions(PlayerId::Two).iter().any(
            |action| matches!(action, Action::DeclareBlocker { attacker: a, .. } if *a == attacker),
        )
    }

    #[test]
    fn nothing_is_offered_as_a_blocker_for_it() {
        let (game, attacker_id, _blocker_id) = attacking(cards::ELUSIVE_KRASIS);
        assert!(
            !offers_block(&game, attacker_id),
            "a printed unblockable attacker takes no blockers"
        );
    }

    /// The same board with an ordinary attacker, so the test above measures
    /// the restriction rather than the setup.
    #[test]
    fn an_ordinary_attacker_can_be_blocked() {
        let (game, attacker_id, _blocker_id) = attacking(cards::SAVANNAH_LIONS);
        assert!(offers_block(&game, attacker_id));
    }

    #[test]
    fn both_identities_report_complete_coverage() {
        let catalog = poc::catalog().expect("catalog builds");
        for definition in [cards::ELUSIVE_KRASIS, cards::SOULSWORN_SPIRIT] {
            let card = catalog.get(definition).expect("the card is cataloged");
            assert_eq!(
                card.rules.implementation_status(),
                ImplementationStatus::Complete,
                "{} should be fully executable",
                card.name,
            );
        }
    }
}
