//! Equip.
//!
//! Equipment and Auras both attach, and until now only Auras did. The
//! differences are the interesting part: an Aura attaches as its own spell
//! resolves and dies when it comes loose, while Equipment attaches through an
//! ability at sorcery speed and simply stays put.

use super::*;
use crate::ImplementationStatus;

fn equipped_board() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    let staff = creature(10_000, cards::COBBLED_WINGS, PlayerId::One);
    let staff_id = staff.card.id;
    game.battlefield.push(staff);
    let troll = creature(10_001, cards::SEDGE_TROLL, PlayerId::One);
    let troll_id = troll.card.id;
    game.battlefield.push(troll);
    game.players[PlayerId::One.index()].mana_pool.colorless = 3;
    (game, staff_id, troll_id)
}

fn equip(game: &mut Game, source: GameObjectId, host: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source: actual,
                targets,
                ..
            } => {
                *actual == source
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|target| *target == Target::Permanent(host))
            }
            _ => false,
        })
        .expect("equip is offered for that creature");
    game.apply(PlayerId::One, action)
        .expect("the ability activates");
    drain_pending(game);
}

fn attached_to(game: &Game, id: GameObjectId) -> Option<GameObjectId> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("still on the battlefield")
        .attached_to
}

#[test]
fn equipping_attaches_and_grants_its_bonus() {
    let (mut game, staff_id, troll_id) = equipped_board();
    let troll = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == troll_id)
        .expect("there");
    assert!(
        !game.permanent_has_executable_keyword(troll, KeywordAbility::Flying),
        "no flying before it is equipped"
    );

    equip(&mut game, staff_id, troll_id);

    assert_eq!(attached_to(&game, staff_id), Some(troll_id));
    let troll = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == troll_id)
        .expect("there");
    assert!(
        game.permanent_has_executable_keyword(troll, KeywordAbility::Flying),
        "the equipped creature has flying"
    );
}

/// Equip is sorcery-speed, which is what stops it being an instant-speed
/// combat trick.
#[test]
fn equip_is_not_offered_outside_a_main_phase() {
    let (mut game, staff_id, _) = equipped_board();
    game.step = Step::DeclareBlockers;

    assert!(
        !game.legal_actions(PlayerId::One).iter().any(|action| {
            matches!(action, Action::ActivateAbility { source, .. } if *source == staff_id)
        }),
        "equip waits for a main phase"
    );
}

/// The difference that matters: an Aura in this position would be in the
/// graveyard, and Equipment is not.
#[test]
fn losing_its_creature_leaves_the_equipment_on_the_battlefield() {
    let (mut game, staff_id, troll_id) = equipped_board();
    equip(&mut game, staff_id, troll_id);

    game.battlefield
        .retain(|permanent| permanent.card.id != troll_id);
    game.check_state_based_actions();

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == staff_id),
        "it stays put rather than dying with its creature"
    );
    assert_eq!(
        attached_to(&game, staff_id),
        None,
        "and it comes loose rather than staying attached to nothing"
    );
}

/// Equipping again moves it rather than attaching twice.
#[test]
fn equipping_a_second_creature_moves_it() {
    let (mut game, staff_id, troll_id) = equipped_board();
    let second = creature(10_002, cards::SAVANNAH_LIONS, PlayerId::One);
    let second_id = second.card.id;
    game.battlefield.push(second);
    game.players[PlayerId::One.index()].mana_pool.colorless = 6;

    equip(&mut game, staff_id, troll_id);
    equip(&mut game, staff_id, second_id);

    assert_eq!(attached_to(&game, staff_id), Some(second_id));
    let troll = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == troll_id)
        .expect("there");
    assert!(
        !game.permanent_has_executable_keyword(troll, KeywordAbility::Flying),
        "the creature it left keeps nothing"
    );
}

/// "As long as equipped creature is a Human" follows the Equipment, so the
/// same Pitchfork gives +1/+1 on one creature and nothing on another.
#[test]
fn a_conditional_bonus_follows_the_attachment() {
    let mut game = ready_game();
    let pitchfork = creature(10_000, cards::SHARPENED_PITCHFORK, PlayerId::One);
    let pitchfork_id = pitchfork.card.id;
    game.battlefield.push(pitchfork);
    // Savannah Lions is a Cat, and Icatian Moneychanger is a Human.
    let cat = creature(10_001, cards::SAVANNAH_LIONS, PlayerId::One);
    let cat_id = cat.card.id;
    game.battlefield.push(cat);
    let human = creature(10_002, cards::ICATIAN_MONEYCHANGER, PlayerId::One);
    let human_id = human.card.id;
    game.battlefield.push(human);
    game.players[PlayerId::One.index()].mana_pool.colorless = 6;

    equip(&mut game, pitchfork_id, cat_id);
    let cat = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == cat_id)
        .expect("there");
    assert_eq!(
        (game.power(cat), game.toughness(cat)),
        (Some(2), Some(1)),
        "a Cat is not a Human, so it gets only first strike"
    );

    equip(&mut game, pitchfork_id, human_id);
    let human = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == human_id)
        .expect("there");
    assert_eq!(
        (game.power(human), game.toughness(human)),
        (Some(1), Some(3)),
        "a 0/2 Human with the conditional +1/+1"
    );
    let cat = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == cat_id)
        .expect("there");
    assert_eq!(
        (game.power(cat), game.toughness(cat)),
        (Some(2), Some(1)),
        "and the Cat is back to printed"
    );
}

#[test]
fn every_equipment_identity_reports_complete_coverage() {
    let catalog = poc::catalog().expect("catalog builds");
    for definition in [
        cards::COBBLED_WINGS,
        cards::SKYBLINDER_STAFF,
        cards::BUTCHERS_CLEAVER,
        cards::SHARPENED_PITCHFORK,
        cards::SILVER_INLAID_DAGGER,
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
