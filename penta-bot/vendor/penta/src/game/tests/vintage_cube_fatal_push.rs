//! Fatal Push: two mana value normally, four once something of yours has
//! left the battlefield this turn.

use super::*;

/// Player One holding a Push, with `victim` on the opponent's battlefield.
fn staged(victim: CardDefinitionId) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let target = game
        .put_onto_battlefield(PlayerId::Two, victim)
        .expect("cataloged");
    drain_pending(&mut game);
    let push = game
        .build_zone(PlayerId::One, &[cards::FATAL_PUSH])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let push_id = push.id;
    game.players[0].hand.push(push);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.permanent_left_battlefield_this_turn = [false; 2];
    game.priority = PlayerId::One;
    (game, push_id, target)
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

/// Casts the Push at `target` and lets it resolve.
fn push(game: &mut Game, spell: GameObjectId, target: GameObjectId) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == spell
                    && choices
                        .iter_targets()
                        .any(|chosen| *chosen == Target::Permanent(target))
            }
            _ => false,
        })
        .expect("any creature is a legal target, whatever it costs");
    game.apply(PlayerId::One, cast).expect("it is castable");
    resolve(game);
}

fn survives(game: &Game, target: GameObjectId) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.id == target)
}

/// Two or less dies without revolt.
#[test]
fn it_kills_a_two_drop() {
    let (mut game, spell, bears) = staged(cards::GRIZZLY_BEARS);
    push(&mut game, spell, bears);

    assert!(!survives(&game, bears), "mana value 2 is within reach");
}

/// Three does not, which is the boundary the card is famous for.
#[test]
fn a_three_drop_survives_without_revolt() {
    let (mut game, spell, specter) = staged(cards::HYPNOTIC_SPECTER);
    push(&mut game, spell, specter);

    assert!(survives(&game, specter), "mana value 3 is one too many");
}

/// The spell still resolves against it, and the creature is untouched: a
/// Push aimed too high is a wasted card, not an illegal one.
#[test]
fn a_creature_out_of_range_is_still_a_legal_target() {
    let (mut game, spell, angel) = staged(cards::SERRA_ANGEL);
    push(&mut game, spell, angel);

    assert!(survives(&game, angel), "mana value 5 is far too many");
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::FATAL_PUSH),
        "and the Push was spent all the same",
    );
}

/// With revolt on, three is comfortably inside the range.
#[test]
fn revolt_reaches_a_three_drop() {
    let (mut game, spell, specter) = staged(cards::HYPNOTIC_SPECTER);
    game.permanent_left_battlefield_this_turn[0] = true;
    push(&mut game, spell, specter);

    assert!(!survives(&game, specter), "revolt raises the ceiling");
}

/// And four is the new boundary: reachable with revolt, and five is not.
#[test]
fn revolt_reaches_four_but_no_further() {
    let (mut game, spell, wall) = staged(cards::WALL_OF_SWORDS);
    game.permanent_left_battlefield_this_turn[0] = true;
    push(&mut game, spell, wall);
    assert!(!survives(&game, wall), "mana value 4 is the new ceiling");

    let (mut game, spell, angel) = staged(cards::SERRA_ANGEL);
    game.permanent_left_battlefield_this_turn[0] = true;
    push(&mut game, spell, angel);
    assert!(survives(&game, angel), "and five is still out of reach");
}

/// A permanent actually leaving is what sets revolt, and it is the
/// controller's own board that counts.
#[test]
fn revolt_is_set_by_your_own_permanent_leaving() {
    let (mut game, spell, specter) = staged(cards::HYPNOTIC_SPECTER);
    let mine = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    let theirs = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.permanent_left_battlefield_this_turn = [false; 2];

    game.destroy_permanent(theirs);
    resolve(&mut game);
    assert!(
        !game.permanent_left_battlefield_this_turn[0],
        "their creature dying is their revolt, not yours",
    );

    game.destroy_permanent(mine);
    resolve(&mut game);
    assert!(game.permanent_left_battlefield_this_turn[0]);

    game.priority = PlayerId::One;
    push(&mut game, spell, specter);
    assert!(!survives(&game, specter));
}
