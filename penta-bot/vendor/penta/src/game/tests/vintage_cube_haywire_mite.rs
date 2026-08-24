//! Haywire Mite: a one-mana body that answers whichever artifact or
//! enchantment the format is afraid of, and pays two life on the way out.

use super::*;

/// The Mite on the battlefield with a green source, and a board holding one
/// of everything its ability might name.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let mite = game
        .put_onto_battlefield(PlayerId::One, cards::HAYWIRE_MITE)
        .expect("cataloged");
    drain_pending(&mut game);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 1);
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, mite)
}

fn activation(game: &Game, mite: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == mite))
}

fn offered_targets(game: &Game, mite: GameObjectId) -> Vec<GameObjectId> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } if source == mite => Some(targets),
            _ => None,
        })
        .flatten()
        .flat_map(|selection| selection.targets().to_vec())
        .filter_map(|target| match target {
            Target::Permanent(id) | Target::Card(id) => Some(id),
            Target::Player(_) | Target::Spell(_) => None,
        })
        .collect()
}

/// Dying gains two life, however it happened.
#[test]
fn dying_gains_two_life() {
    let (mut game, mite) = staged();
    game.players[0].life = 20;

    game.move_permanents_to_graveyard(&[mite]);
    drain_pending(&mut game);

    assert_eq!(game.players[0].life, 22);
}

/// The ability exiles a noncreature artifact.
#[test]
fn it_exiles_a_noncreature_artifact() {
    let (mut game, mite) = staged();
    game.put_onto_battlefield(PlayerId::Two, cards::BLACK_LOTUS)
        .expect("cataloged");
    drain_pending(&mut game);

    let action = activation(&game, mite).expect("one green and a sacrifice");
    game.apply(PlayerId::One, action).expect("it activates");
    drain_pending(&mut game);

    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::BLACK_LOTUS),
        "the Lotus is exiled, not destroyed",
    );
}

/// Sacrificing itself is a cost, so the Mite is gone and its own dies
/// trigger still pays out.
#[test]
fn activating_it_sacrifices_it_and_gains_the_life() {
    let (mut game, mite) = staged();
    game.put_onto_battlefield(PlayerId::Two, cards::BLACK_LOTUS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.players[0].life = 20;

    let action = activation(&game, mite).expect("one green and a sacrifice");
    game.apply(PlayerId::One, action).expect("it activates");
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != mite),
        "it sacrificed itself as a cost",
    );
    assert_eq!(game.players[0].life, 22, "and died on the way, as it says");
}

/// "Noncreature": an artifact creature is not on the menu, and neither is
/// an ordinary creature.
#[test]
fn it_leaves_creatures_alone() {
    let (mut game, mite) = staged();
    let other_mite = game
        .put_onto_battlefield(PlayerId::Two, cards::HAYWIRE_MITE)
        .expect("cataloged");
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    let lotus = game
        .put_onto_battlefield(PlayerId::Two, cards::BLACK_LOTUS)
        .expect("cataloged");
    drain_pending(&mut game);

    let offered = offered_targets(&game, mite);

    assert!(
        offered.contains(&lotus),
        "the Lotus is a noncreature artifact"
    );
    assert!(
        !offered.contains(&other_mite),
        "an artifact creature is still a creature: {offered:?}",
    );
    assert!(!offered.contains(&bears));
}

/// Without green mana there is nothing to activate it with.
#[test]
fn it_needs_the_green_mana() {
    let (mut game, mite) = staged();
    game.players[0].mana_pool = ManaPool::default();
    game.put_onto_battlefield(PlayerId::Two, cards::BLACK_LOTUS)
        .expect("cataloged");
    drain_pending(&mut game);

    assert!(
        activation(&game, mite).is_none(),
        "every deck can cast it; not every deck can use it",
    );
}
