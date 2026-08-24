//! Sentinel of the Nameless City: a 3/4 that hands you a Map for arriving
//! and another for every attack.

use super::*;

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].library.clear();
    game.players[PlayerId::One.index()].library.push(card(
        95_000,
        cards::GRIZZLY_BEARS,
        PlayerId::One,
    ));
    let sentinel = game
        .put_onto_battlefield(PlayerId::One, cards::SENTINEL_OF_THE_NAMELESS_CITY)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, sentinel)
}

fn maps(game: &Game) -> usize {
    game.battlefield
        .iter()
        .filter(|permanent| {
            game.effective_subtypes(permanent).contains(&"Map")
                && permanent.controller == PlayerId::One
        })
        .count()
}

/// Arriving makes one.
#[test]
fn arriving_makes_a_map() {
    let (game, sentinel) = staged();
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == sentinel)
        .expect("it is there");

    assert_eq!(
        (game.power(permanent), game.toughness(permanent)),
        (Some(3), Some(4))
    );
    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Vigilance));
    assert_eq!(maps(&game), 1);
}

/// Attacking makes another, and vigilance means it is still untapped
/// afterwards.
#[test]
fn attacking_makes_another_and_leaves_it_untapped() {
    let (mut game, sentinel) = staged();

    game.step = Step::DeclareAttackers;
    game.declare_attacker(sentinel, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    drain_pending(&mut game);

    assert_eq!(maps(&game), 2);
    assert!(
        !game
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == sentinel)
            .expect("it is there")
            .tapped,
        "vigilance keeps it back",
    );
}

/// The Map it makes is the real one: a mana, a tap, and itself for an
/// explore.
#[test]
fn the_map_explores() {
    let (mut game, sentinel) = staged();
    let map = game
        .battlefield
        .iter()
        .find(|permanent| game.effective_subtypes(permanent).contains(&"Map"))
        .expect("the Map is there")
        .card
        .id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    game.step = Step::PrecombatMain;

    let explore = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == map
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(sentinel))
            }
            _ => false,
        })
        .expect("one mana and the Map itself buys an explore");
    game.apply(PlayerId::One, explore).expect("it activates");
    drain_pending(&mut game);

    let explored = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == sentinel)
        .expect("it is there");
    assert_eq!(
        explored.counters(CounterKind::PlusOnePlusOne),
        1,
        "a nonland on top means a counter",
    );
    assert_eq!(maps(&game), 0, "the Map is spent");
}
