//! Kappa Cannoneer: a six-mana artifact that the rest of your artifacts pay
//! for, grow, and make unblockable.

use super::*;

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    let cannoneer = game
        .put_onto_battlefield(PlayerId::One, cards::KAPPA_CANNONEER)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, cannoneer)
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

/// Its own arrival counts: it is already a 5/5 by the time anyone sees it.
#[test]
fn it_grows_on_its_own_arrival() {
    let (game, cannoneer) = staged();
    let turtle = permanent(&game, cannoneer);

    assert_eq!(turtle.counters(CounterKind::PlusOnePlusOne), 1);
    assert_eq!(
        (game.power(turtle), game.toughness(turtle)),
        (Some(5), Some(5))
    );
}

/// Every artifact after it is another counter, creature or not.
#[test]
fn every_artifact_afterwards_grows_it() {
    let (mut game, cannoneer) = staged();

    game.put_onto_battlefield(PlayerId::One, cards::MOX_JET)
        .expect("cataloged");
    drain_pending(&mut game);
    assert_eq!(
        permanent(&game, cannoneer).counters(CounterKind::PlusOnePlusOne),
        2,
        "a Mox is an artifact",
    );

    // Somebody else's artifact is not one you control.
    game.put_onto_battlefield(PlayerId::Two, cards::BLACK_LOTUS)
        .expect("cataloged");
    drain_pending(&mut game);
    assert_eq!(
        permanent(&game, cannoneer).counters(CounterKind::PlusOnePlusOne),
        2,
    );
}

/// The trigger also makes it unblockable for the turn.
#[test]
fn an_artifact_makes_it_unblockable_for_the_turn() {
    let (mut game, cannoneer) = staged();
    game.put_onto_battlefield(PlayerId::One, cards::MOX_JET)
        .expect("cataloged");
    drain_pending(&mut game);

    let blocker = creature(98_000, cards::SERRA_ANGEL, PlayerId::Two);
    game.battlefield.push(blocker);
    game.step = Step::DeclareBlockers;
    game.attackers_declared = true;
    if let Some(attacker) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == cannoneer)
    {
        attacker.attacking = true;
        attacker.attack_defender = Some(AttackDefender::Player(PlayerId::Two));
    }

    assert!(
        !game.legal_actions(PlayerId::Two).iter().any(
            |action| matches!(action, Action::DeclareBlocker { attacker, .. } if *attacker == cannoneer)
        ),
        "nothing may block it this turn",
    );
}

/// Improvise and ward are both printed on it.
#[test]
fn it_improvises_and_wards() {
    let (game, cannoneer) = staged();
    let turtle = permanent(&game, cannoneer);

    assert!(game.permanent_has_executable_keyword(turtle, KeywordAbility::Improvise));
    assert!(
        game.effective_rules(turtle)
            .is_some_and(|rules| rules.rules_text().contains("Ward {4}")),
    );
}
