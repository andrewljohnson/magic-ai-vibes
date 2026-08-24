//! Prismatic Ending: converge, and the colours a spell was paid with.

use super::*;

/// Puts one mana of each named colour into the pool.
fn pool_of(game: &mut Game, colors: &[ManaColor]) {
    for color in colors {
        game.add_unrestricted_mana(PlayerId::One, *color, 1);
    }
}

/// Casts the Ending at `target` for `x`, then resolves it.
fn cast_ending(game: &mut Game, ending: CardInstanceId, target: GameObjectId, x: u16) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == ending
                    && choices.x() == x
                    && choices
                        .targets()
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(target))
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("a cast at X={x} is offered"));
    game.apply(PlayerId::One, cast).expect("it is cast");
    pass_priority_pair(game);
    drain_pending(game);
}

/// Builds a board with one target of the given mana value, and an Ending in
/// hand paid for out of `colors`.
fn staged(target: CardDefinitionId, colors: &[ManaColor]) -> (Game, CardInstanceId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let victim = creature(94_000, target, PlayerId::Two);
    let victim_id = victim.card.id;
    game.battlefield.push(victim);
    let ending = card(94_001, cards::PRISMATIC_ENDING, PlayerId::One);
    let ending_id = ending.id;
    game.players[0].hand.push(ending);
    pool_of(&mut game, colors);
    (game, ending_id, victim_id)
}

fn exiled(game: &Game, id: GameObjectId) -> bool {
    game.battlefield
        .iter()
        .all(|permanent| permanent.card.id != id)
}

/// One white mana is one colour, so it answers a one-drop and nothing more.
#[test]
fn one_color_exiles_a_one_drop() {
    let (mut game, ending_id, lions) = staged(cards::SAVANNAH_LIONS, &[ManaColor::White]);

    cast_ending(&mut game, ending_id, lions, 0);

    assert!(exiled(&game, lions), "a mana value of one, and one colour");
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::SAVANNAH_LIONS),
        "exiled rather than destroyed",
    );
}

/// The same cast against a two-drop does nothing: the spell resolves, the
/// condition fails, and the creature stays.
#[test]
fn one_color_leaves_a_two_drop_alone() {
    let (mut game, ending_id, bears) = staged(cards::GRIZZLY_BEARS, &[ManaColor::White]);

    cast_ending(&mut game, ending_id, bears, 0);

    assert!(!exiled(&game, bears), "two is more than one colour");
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::PRISMATIC_ENDING),
        "and the spell was still spent",
    );
}

/// Paying the X with a second colour buys a second point, which is the whole
/// card: {W} plus one blue answers a two-drop.
#[test]
fn a_second_color_paid_into_x_exiles_a_two_drop() {
    let (mut game, ending_id, bears) =
        staged(cards::GRIZZLY_BEARS, &[ManaColor::White, ManaColor::Blue]);

    cast_ending(&mut game, ending_id, bears, 1);

    assert!(exiled(&game, bears), "white and blue is two colours");
}

/// Two mana of one colour is still one colour. The generic portion spreads
/// across colours where it can, but it cannot invent a colour that is not
/// in the pool.
#[test]
fn a_second_mana_of_the_same_color_buys_nothing() {
    let (mut game, ending_id, bears) =
        staged(cards::GRIZZLY_BEARS, &[ManaColor::White, ManaColor::White]);

    cast_ending(&mut game, ending_id, bears, 1);

    assert!(!exiled(&game, bears), "white and white is one colour");
}

/// The generic portion is spent across colours rather than draining one:
/// with two white and one blue available, an X of one takes the blue.
#[test]
fn the_generic_portion_reaches_for_a_new_color_first() {
    let (mut game, ending_id, bears) = staged(
        cards::GRIZZLY_BEARS,
        &[ManaColor::White, ManaColor::White, ManaColor::Blue],
    );

    cast_ending(&mut game, ending_id, bears, 1);

    assert!(
        exiled(&game, bears),
        "a payment that drained the whites first would have counted one colour",
    );
    assert_eq!(
        game.players[0].mana_pool.white, 1,
        "and the leftover is the white it did not need",
    );
}

/// Colourless is a mana type, not a colour, and never adds to the count.
#[test]
fn colorless_mana_is_not_a_color() {
    let (mut game, ending_id, bears) = staged(
        cards::GRIZZLY_BEARS,
        &[ManaColor::White, ManaColor::Colorless],
    );

    cast_ending(&mut game, ending_id, bears, 1);

    assert!(!exiled(&game, bears), "one colour and one colourless");
}
