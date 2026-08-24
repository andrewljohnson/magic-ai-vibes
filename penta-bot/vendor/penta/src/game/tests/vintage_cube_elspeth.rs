//! Elspeth, Knight-Errant: two plus abilities that both do something, and
//! an emblem that turns off every answer aimed at permanents.

use super::*;

/// Elspeth on the battlefield with a creature beside her.
fn staged() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let elspeth = game
        .put_onto_battlefield(PlayerId::One, cards::ELSPETH_KNIGHT_ERRANT)
        .expect("cataloged");
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [2, 1];
    drain_pending(&mut game);
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, elspeth, bears)
}

fn loyalty(game: &Game, elspeth: GameObjectId) -> u16 {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == elspeth)
        .map_or(0, |permanent| permanent.counters(CounterKind::Loyalty))
}

/// Every activation Elspeth is offering, in printed order.
fn activations(game: &Game, elspeth: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == elspeth),
        )
        .collect()
}

/// She arrives on four loyalty.
#[test]
fn she_enters_with_four_loyalty() {
    let (game, elspeth, _bears) = staged();

    assert_eq!(loyalty(&game, elspeth), 4);
}

/// The first plus makes a 1/1 white Soldier and raises her to five.
#[test]
fn the_first_plus_makes_a_soldier() {
    let (mut game, elspeth, _bears) = staged();
    let action = activations(&game, elspeth)
        .into_iter()
        .next()
        .expect("the token ability is offered");

    game.apply(PlayerId::One, action).expect("it activates");
    drain_pending(&mut game);

    let soldier = game
        .battlefield
        .iter()
        .find(|permanent| game.effective_subtypes(permanent).contains(&"Soldier"))
        .expect("the Soldier is there");
    assert_eq!(game.power(soldier), Some(1));
    assert_eq!(game.toughness(soldier), Some(1));
    assert_eq!(loyalty(&game, elspeth), 5);
}

/// The second plus makes a creature bigger and gives it flying for the turn.
#[test]
fn the_second_plus_pumps_and_grants_flying() {
    let (mut game, elspeth, bears) = staged();
    let action = activations(&game, elspeth)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility { targets, .. } => targets
                .iter()
                .flat_map(|selection| selection.targets().to_vec())
                .any(|target| target == Target::Permanent(bears)),
            _ => false,
        })
        .expect("the pump ability can name the Bears");

    game.apply(PlayerId::One, action).expect("it activates");
    drain_pending(&mut game);

    let pumped = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the Bears are there");
    assert_eq!(game.power(pumped), Some(5));
    assert_eq!(game.toughness(pumped), Some(5));
    assert!(game.permanent_has_executable_keyword(pumped, KeywordAbility::Flying));
    assert_eq!(loyalty(&game, elspeth), 5, "it is a plus ability");
}

/// The ultimate is out of reach on four loyalty.
#[test]
fn the_ultimate_needs_eight_loyalty() {
    let (game, elspeth, _bears) = staged();

    assert_eq!(
        activations(&game, elspeth).len(),
        2,
        "only the two plus abilities are payable",
    );
}

/// With the loyalty for it, the emblem gives everything you control
/// indestructible -- and the emblem outlives Elspeth herself.
#[test]
fn the_emblem_makes_your_permanents_indestructible() {
    let (mut game, elspeth, bears) = staged();
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == elspeth)
    {
        permanent.set_counters(CounterKind::Loyalty, 8);
    }
    let ultimate = activations(&game, elspeth)
        .into_iter()
        .last()
        .expect("eight loyalty pays for it");

    game.apply(PlayerId::One, ultimate).expect("it activates");
    drain_pending(&mut game);

    assert_eq!(game.emblems.len(), 1, "the emblem is out");
    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != elspeth),
        "and she paid all her loyalty for it",
    );

    let survivor = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the Bears are there");
    assert!(game.permanent_has_executable_keyword(survivor, KeywordAbility::Indestructible));
}

/// It is your permanents, not theirs.
#[test]
fn the_emblem_leaves_their_permanents_alone() {
    let (mut game, elspeth, _bears) = staged();
    let theirs = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == elspeth)
    {
        permanent.set_counters(CounterKind::Loyalty, 8);
    }
    let ultimate = activations(&game, elspeth)
        .into_iter()
        .last()
        .expect("eight loyalty pays for it");
    game.apply(PlayerId::One, ultimate).expect("it activates");
    drain_pending(&mut game);

    let opposing = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == theirs)
        .expect("their Bears are there");
    assert!(!game.permanent_has_executable_keyword(opposing, KeywordAbility::Indestructible));
}
