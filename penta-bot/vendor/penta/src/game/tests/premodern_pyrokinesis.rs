//! Pyrokinesis: a fixed total split among as many creatures as you like.
//!
//! The card prints no ceiling on the number of targets, but the division is
//! its own ceiling -- every target must be assigned at least one damage.

use super::*;

fn ready() -> Game {
    let mut game = ready_game();
    game.turn = 5;
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game
}

fn resolve(game: &mut Game) {
    for _ in 0..8 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// Pyrokinesis splits a fixed four damage among however many creatures the
/// caster names. There is no printed ceiling, but the division supplies one:
/// every target takes at least one, so four targets is the most it reaches.
#[test]
fn pyrokinesis_divides_four_damage_and_cannot_name_a_fifth_creature() {
    let mut game = ready();
    for index in 0..5 {
        game.battlefield
            .push(creature(10_000 + index, cards::SERRA_ANGEL, PlayerId::Two));
    }
    let pyro = card(20_000, cards::PYROKINESIS, PlayerId::One);
    let pyro_id = pyro.id;
    game.players[PlayerId::One.index()].hand.push(pyro);
    game.players[PlayerId::One.index()].mana_pool.red = 2;
    game.players[PlayerId::One.index()].mana_pool.colorless = 4;

    let widest = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::CastSpell { card, choices, .. } if card == pyro_id => Some(
                choices
                    .targets()
                    .iter()
                    .map(|slot| slot.targets().len())
                    .sum::<usize>(),
            ),
            _ => None,
        })
        .max()
        .expect("Pyrokinesis is castable");
    assert_eq!(widest, 4, "four damage cannot be split more than four ways");
}

/// And the four damage actually lands, split across the creatures named.
#[test]
fn pyrokinesis_deals_its_four_damage() {
    let mut game = ready();
    game.battlefield
        .push(creature(10_000, cards::SERRA_ANGEL, PlayerId::Two));
    let pyro = card(20_000, cards::PYROKINESIS, PlayerId::One);
    let pyro_id = pyro.id;
    game.players[PlayerId::One.index()].hand.push(pyro);
    game.players[PlayerId::One.index()].mana_pool.red = 2;
    game.players[PlayerId::One.index()].mana_pool.colorless = 4;

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == pyro_id))
        .expect("Pyrokinesis is castable");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == CardInstanceId(10_000)),
        "all four went to the lone target, which kills a 4/4",
    );
}
