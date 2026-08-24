//! Glorybringer, and exert: a choice made as it attacks, paid for with the
//! next untap step.

use super::*;

/// A declare-attackers step with an untapped Glorybringer and a pair of
/// creatures across the table -- one of them a Dragon.
fn staged() -> (Game, GameObjectId, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let dragon = game
        .put_onto_battlefield(PlayerId::One, cards::GLORYBRINGER)
        .expect("cataloged");
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    let their_dragon = game
        .put_onto_battlefield(PlayerId::Two, cards::GLORYBRINGER)
        .expect("cataloged");
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [2, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.priority = PlayerId::One;
    (game, dragon, bears, their_dragon)
}

fn exert_actions(game: &Game) -> Vec<GameObjectId> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ExertAttacker { attacker } => Some(attacker),
            _ => None,
        })
        .collect()
}

fn attack_with(game: &mut Game, attacker: GameObjectId) {
    game.apply(
        PlayerId::One,
        Action::DeclareAttacker {
            attacker,
            defender: AttackDefender::Player(PlayerId::Two),
        },
    )
    .expect("it attacks");
}

/// Exert is offered only once the creature is actually attacking.
#[test]
fn exert_is_offered_only_to_a_declared_attacker() {
    let (mut game, dragon, _bears, _their_dragon) = staged();
    assert!(
        exert_actions(&game).is_empty(),
        "nothing has been declared yet",
    );

    attack_with(&mut game, dragon);

    assert_eq!(exert_actions(&game), vec![dragon]);
}

/// A creature with no exert clause is never offered it.
#[test]
fn a_creature_without_the_clause_is_never_offered_it() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [2, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.priority = PlayerId::One;
    attack_with(&mut game, bears);

    assert!(exert_actions(&game).is_empty());
}

/// Exerting costs the next untap step, and only one however the turn goes.
#[test]
fn exerting_owes_an_untap_step() {
    let (mut game, dragon, _bears, _their_dragon) = staged();
    attack_with(&mut game, dragon);

    game.apply(PlayerId::One, Action::ExertAttacker { attacker: dragon })
        .expect("it exerts");

    let exerted = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == dragon)
        .expect("it is still there");
    assert!(exerted.exerted);
    assert_eq!(exerted.skipped_untap_steps, 1);
    assert!(
        exert_actions(&game).is_empty(),
        "and it cannot be exerted twice",
    );
}

/// "When you do": the reflexive trigger deals four to something they
/// control.
#[test]
fn exerting_deals_four_to_their_creature() {
    let (mut game, dragon, bears, _their_dragon) = staged();
    attack_with(&mut game, dragon);
    game.apply(PlayerId::One, Action::ExertAttacker { attacker: dragon })
        .expect("it exerts");
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("the declaration finishes");
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != bears),
        "four damage kills a 2/2",
    );
}

/// Not exerting fires nothing.
#[test]
fn attacking_without_exerting_deals_no_damage() {
    let (mut game, dragon, bears, _their_dragon) = staged();
    attack_with(&mut game, dragon);
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("the declaration finishes");
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears),
        "the bear is untouched",
    );
    let attacker = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == dragon)
        .expect("it is still there");
    assert_eq!(attacker.skipped_untap_steps, 0, "and nothing was owed");
}

/// "Non-Dragon": their own Glorybringer is not a legal target.
#[test]
fn the_trigger_does_not_name_a_dragon() {
    let (mut game, dragon, bears, their_dragon) = staged();
    attack_with(&mut game, dragon);
    game.apply(PlayerId::One, Action::ExertAttacker { attacker: dragon })
        .expect("it exerts");
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("the declaration finishes");

    let choice = game
        .observe(PlayerId::One)
        .decision
        .expect("the reflexive trigger asks for its target");
    let offered = choice
        .options
        .iter()
        .filter_map(|option| option.card.map(|(instance, _)| instance))
        .collect::<Vec<_>>();

    assert!(offered.contains(&bears), "the bear is on the menu");
    assert!(
        !offered.contains(&their_dragon),
        "their Dragon is not: {offered:?}",
    );
}

/// Flying and haste are printed on it, so it attacks the turn it lands.
#[test]
fn it_flies_and_has_haste() {
    let (game, dragon, _bears, _their_dragon) = staged();
    let glorybringer = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == dragon)
        .expect("it is there");

    assert!(game.permanent_has_executable_keyword(glorybringer, KeywordAbility::Flying));
    assert!(game.permanent_has_executable_keyword(glorybringer, KeywordAbility::Haste));
}
