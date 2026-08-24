//! Talisman of Progress: colourless for nothing, or white or blue for a
//! life.

use super::*;

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let talisman = game
        .put_onto_battlefield(PlayerId::One, cards::TALISMAN_OF_PROGRESS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.players[0].life = 20;
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, talisman)
}

fn mana_action(game: &Game, source: GameObjectId, color: ManaColor) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateManaAbility {
                source: activated,
                color: made,
                ..
            } => *activated == source && *made == color,
            _ => false,
        })
}

/// Colourless costs nothing.
#[test]
fn colorless_is_free() {
    let (mut game, talisman) = staged();

    let colorless = mana_action(&game, talisman, ManaColor::Colorless).expect("it makes {C}");
    game.apply(PlayerId::One, colorless).expect("it activates");

    assert_eq!(game.players[0].mana.len(), 1);
    assert_eq!(game.players[0].life, 20, "and takes nothing for it");
}

/// Either colour costs a life, and the damage comes from the artifact.
#[test]
fn a_color_costs_a_life() {
    for color in [ManaColor::White, ManaColor::Blue] {
        let (mut game, talisman) = staged();

        let colored = mana_action(&game, talisman, color).expect("it makes both colours");
        game.apply(PlayerId::One, colored).expect("it activates");
        game.check_state_based_actions();

        assert_eq!(game.players[0].mana.len(), 1);
        assert_eq!(game.players[0].life, 19, "one damage from the artifact");
    }
}

/// It makes no colour it does not print.
#[test]
fn it_makes_only_its_two_colors() {
    let (game, talisman) = staged();

    for color in [ManaColor::Red, ManaColor::Black, ManaColor::Green] {
        assert!(
            mana_action(&game, talisman, color).is_none(),
            "{color:?} is not one of its colours",
        );
    }
}

/// Tapping for one closes the other: the tap is the cost of both.
#[test]
fn the_tap_pays_for_only_one_of_them() {
    let (mut game, talisman) = staged();

    let colorless = mana_action(&game, talisman, ManaColor::Colorless).expect("it makes {C}");
    game.apply(PlayerId::One, colorless).expect("it activates");

    assert!(mana_action(&game, talisman, ManaColor::White).is_none());
    assert!(mana_action(&game, talisman, ManaColor::Colorless).is_none());
}

/// The card prints damage, not a life payment. Anything watching for damage
/// sees it, and nothing watching for life loss does.
#[test]
fn the_life_it_costs_is_dealt_as_damage() {
    let (mut game, talisman) = staged();
    let before = game.events.len();

    let white = mana_action(&game, talisman, ManaColor::White).expect("it makes {W}");
    game.apply(PlayerId::One, white).expect("it activates");
    game.check_state_based_actions();

    let events = &game.events[before..];
    assert!(
        events.iter().any(|event| matches!(
            event,
            GameEvent::DamageDealt {
                player: PlayerId::One,
                amount: 1,
            }
        )),
        "one damage was dealt: {events:?}",
    );
    assert!(
        !events
            .iter()
            .any(|event| matches!(event, GameEvent::LifeLost { .. })),
        "and nothing was paid",
    );
}
