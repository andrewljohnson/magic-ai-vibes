//! Path to Exile: removal that pays its victim, in either direction.

use super::*;

/// Resolves whatever is on the stack, answering nothing.
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

fn cast_path(game: &mut Game, path: CardInstanceId, target: GameObjectId) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == path
                    && choices
                        .targets()
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(target))
            }
            _ => false,
        })
        .expect("one white mana casts it at that creature");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(game);
    drain_pending(game);
}

/// The ordinary use: their creature is exiled, and they are paid one basic
/// land, tapped, for the trouble.
#[test]
fn path_exiles_their_creature_and_pays_them_a_tapped_basic() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = creature(96_000, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    game.players[1].library.clear();
    game.players[1]
        .library
        .push(card(96_001, cards::FOREST, PlayerId::Two));
    let path = card(96_002, cards::PATH_TO_EXILE, PlayerId::One);
    let path_id = path.id;
    game.players[0].hand.push(path);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 1);

    cast_path(&mut game, path_id, bears_id);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears_id),
        "the creature is gone",
    );
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
        "exiled rather than destroyed",
    );
    let forest = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::FOREST)
        .expect("its controller found a basic land");
    assert_eq!(forest.controller, PlayerId::Two, "the land is theirs");
    assert!(forest.tapped, "and it arrives tapped");
    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.controller != PlayerId::One),
        "and nothing came to the caster",
    );
}

/// Pointed at your own creature it is a Rampant Growth: the searcher is the
/// creature's controller, whoever that is.
#[test]
fn path_on_your_own_creature_ramps_you() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = creature(96_010, cards::GRIZZLY_BEARS, PlayerId::One);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    game.players[0].library.clear();
    game.players[0]
        .library
        .push(card(96_011, cards::PLAINS, PlayerId::One));
    let path = card(96_012, cards::PATH_TO_EXILE, PlayerId::One);
    let path_id = path.id;
    game.players[0].hand.push(path);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 1);

    cast_path(&mut game, path_id, bears_id);

    let plains = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::PLAINS)
        .expect("you searched your own library");
    assert_eq!(plains.controller, PlayerId::One);
    assert!(plains.tapped);
}

/// The search is a "may", and a library with no basic land answers it the
/// same way a decline does: the creature is still exiled.
#[test]
fn a_library_without_a_basic_still_loses_the_creature() {
    let mut game = ready_game();
    game.battlefield.clear();
    let bears = creature(96_020, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    game.players[1].library.clear();
    game.players[1]
        .library
        .push(card(96_021, cards::GRIZZLY_BEARS, PlayerId::Two));
    let path = card(96_022, cards::PATH_TO_EXILE, PlayerId::One);
    let path_id = path.id;
    game.players[0].hand.push(path);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 1);

    cast_path(&mut game, path_id, bears_id);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears_id),
        "the exile is not contingent on the search",
    );
    assert_eq!(
        game.players[1].library.len(),
        1,
        "and nothing was taken from a library that held no basic land",
    );
}
