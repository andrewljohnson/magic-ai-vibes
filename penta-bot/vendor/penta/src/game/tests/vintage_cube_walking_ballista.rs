//! Walking Ballista: two mana a counter, and every counter is a point of
//! damage on the way back out.

use super::*;

/// Player One with a Ballista in hand and enough mana for X of `x`.
fn staged(x: u16) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let ballista = game
        .build_zone(PlayerId::One, &[cards::WALKING_BALLISTA])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = ballista.id;
    game.players[0].hand.push(ballista);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2 * x);
    game.priority = PlayerId::One;
    (game, id)
}

fn resolve(game: &mut Game) {
    for _ in 0..16 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
    game.check_state_based_actions();
}

/// Casts the Ballista for `x` and lets it resolve.
fn cast(game: &mut Game, ballista: GameObjectId, x: u16) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => *card == ballista && choices.x() == x,
            _ => false,
        })
        .unwrap_or_else(|| panic!("a Ballista for X={x} is castable"));
    game.apply(PlayerId::One, action).expect("it is castable");
    resolve(game);
}

fn on_battlefield(game: &Game) -> Option<&Permanent> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::WALKING_BALLISTA)
}

fn counters(game: &Game) -> u16 {
    on_battlefield(game).map_or(0, |permanent| {
        permanent.counters(CounterKind::PlusOnePlusOne)
    })
}

/// Every ability the Ballista is offering right now.
fn abilities(game: &Game, ballista: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == ballista))
        .collect()
}

/// Shoots the opponent once.
fn shoot(game: &mut Game, ballista: GameObjectId) {
    let shot = abilities(game, ballista)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility { targets, .. } => targets
                .iter()
                .any(|selection| selection.targets().contains(&Target::Player(PlayerId::Two))),
            _ => false,
        })
        .expect("a counter on it is a shot at the opponent");
    game.apply(PlayerId::One, shot).expect("it activates");
    resolve(game);
}

/// X counters arrive with it, and the body is the counters.
#[test]
fn it_enters_with_x_counters() {
    let (mut game, ballista) = staged(3);
    cast(&mut game, ballista, 3);

    let permanent = on_battlefield(&game).expect("it resolved");
    assert_eq!(permanent.counters(CounterKind::PlusOnePlusOne), 3);
    assert_eq!(game.power(permanent), Some(3), "a 0/0 with three counters");
}

/// Cast for nothing it is a 0/0 and dies at once, which is what makes X
/// matter rather than being decoration.
#[test]
fn cast_for_nothing_it_dies_immediately() {
    let (mut game, ballista) = staged(0);
    cast(&mut game, ballista, 0);

    assert!(on_battlefield(&game).is_none(), "a 0/0 is a 0/0");
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::WALKING_BALLISTA),
    );
}

/// A counter comes off as the cost and a point of damage goes across.
#[test]
fn removing_a_counter_deals_a_damage() {
    let (mut game, ballista) = staged(2);
    cast(&mut game, ballista, 2);
    let id = on_battlefield(&game).expect("it resolved").card.id;

    shoot(&mut game, id);

    assert_eq!(game.players[1].life, 19, "one point");
    assert_eq!(counters(&game), 1, "and one counter left");
}

/// The last counter shoots, and then the Ballista is a 0/0.
#[test]
fn the_last_counter_takes_it_with_it() {
    let (mut game, ballista) = staged(1);
    cast(&mut game, ballista, 1);
    let id = on_battlefield(&game).expect("it resolved").card.id;

    shoot(&mut game, id);

    assert_eq!(game.players[1].life, 19);
    assert!(
        on_battlefield(&game).is_none(),
        "nothing holds a 0/0 up once the counter has paid for a shot",
    );
}

/// Four mana buys another counter, and the Ballista grows.
#[test]
fn four_mana_buys_a_counter() {
    let (mut game, ballista) = staged(1);
    cast(&mut game, ballista, 1);
    let id = on_battlefield(&game).expect("it resolved").card.id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);

    let grow = abilities(&game, id)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { targets, .. } if targets.is_empty()))
        .expect("four mana is on offer");
    game.apply(PlayerId::One, grow).expect("it activates");
    resolve(&mut game);

    assert_eq!(counters(&game), 2);
}

/// With no counters on it there is nothing to shoot with.
#[test]
fn a_ballista_with_no_counters_cannot_shoot() {
    let (mut game, ballista) = staged(1);
    cast(&mut game, ballista, 1);
    let id = on_battlefield(&game).expect("it resolved").card.id;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == id)
    {
        permanent.set_counters(CounterKind::PlusOnePlusOne, 0);
    }

    assert!(
        abilities(&game, id).iter().all(
            |action| matches!(action, Action::ActivateAbility { targets, .. } if targets.is_empty())
        ),
        "the shot needs a counter to pay with",
    );
}
