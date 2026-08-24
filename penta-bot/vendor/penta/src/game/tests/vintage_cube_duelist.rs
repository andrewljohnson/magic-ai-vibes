//! Duelist of the Mind: a power that counts draws, and a crime that pays
//! once a turn.

use super::*;

/// Answers every pending decision, saying yes to any "may", then resolves
/// whatever is left on the stack.
fn settle(game: &mut Game) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .find(|option| option.label != "Decline")
                .or_else(|| decision.options.first())
                .map(|option| vec![option.id])
                .unwrap_or_default();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the decision accepts what it offered");
            continue;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let duelist = game
        .put_onto_battlefield(PlayerId::One, cards::DUELIST_OF_THE_MIND)
        .expect("cataloged");
    drain_pending(&mut game);
    game.cards_drawn_this_turn = [0; 2];
    (game, duelist)
}

fn power_of(game: &Game, id: GameObjectId) -> Option<i16> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .and_then(|permanent| game.power(permanent))
}

/// Nothing drawn is a zero-power flier; every draw is another point.
#[test]
fn the_power_counts_the_cards_you_have_drawn_this_turn() {
    let (mut game, duelist) = staged();

    assert_eq!(power_of(&game, duelist), Some(0), "no draws yet");
    game.draw_cards(PlayerId::One, 3);
    drain_pending(&mut game);
    assert_eq!(power_of(&game, duelist), Some(3));

    // The opponent's draws are theirs, not yours.
    game.draw_cards(PlayerId::Two, 2);
    drain_pending(&mut game);
    assert_eq!(power_of(&game, duelist), Some(3), "still only your own");
}

/// Targeting an opponent's creature is a crime, and it pays out a draw and
/// a discard.
#[test]
fn targeting_an_opponents_creature_is_a_crime() {
    let (mut game, _duelist) = staged();
    let theirs = creature(90_000, cards::GRIZZLY_BEARS, PlayerId::Two);
    let theirs_id = theirs.card.id;
    game.battlefield.push(theirs);
    let bolt = card(90_001, cards::LIGHTNING_BOLT, PlayerId::One);
    let bolt_id = bolt.id;
    game.players[0].hand.push(bolt);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);
    let hand_before = game.players[0].hand.len();

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == bolt_id
                    && choices
                        .targets()
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|target| *target == Target::Permanent(theirs_id))
            }
            _ => false,
        })
        .expect("the Bolt can point at their creature");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);
    drain_pending(&mut game);

    assert_eq!(
        game.players[0].hand.len(),
        hand_before - 1,
        "the Bolt left, and the draw and discard cancel out",
    );
    assert_eq!(
        game.cards_drawn_this_turn[0], 1,
        "the crime paid out a draw"
    );
}

/// Targeting your own creature is not a crime.
#[test]
fn targeting_your_own_creature_is_not_a_crime() {
    let (mut game, _duelist) = staged();
    let mine = creature(90_010, cards::GRIZZLY_BEARS, PlayerId::One);
    let mine_id = mine.card.id;
    game.battlefield.push(mine);
    let bolt = card(90_011, cards::LIGHTNING_BOLT, PlayerId::One);
    let bolt_id = bolt.id;
    game.players[0].hand.push(bolt);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == bolt_id
                    && choices
                        .targets()
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|target| *target == Target::Permanent(mine_id))
            }
            _ => false,
        })
        .expect("the Bolt can point at your own creature");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);
    drain_pending(&mut game);

    assert_eq!(
        game.cards_drawn_this_turn[0], 0,
        "nothing of theirs was pointed at",
    );
}

/// The ability triggers only once each turn, however many crimes follow.
#[test]
fn the_ability_pays_out_only_once_each_turn() {
    let (mut game, _duelist) = staged();
    for (offset, id) in (90_020..90_023).enumerate() {
        let bolt = card(id, cards::LIGHTNING_BOLT, PlayerId::One);
        game.players[0].hand.push(bolt);
        let _ = offset;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 3);

    for _ in 0..3 {
        let cast =
            game.legal_actions(PlayerId::One)
                .into_iter()
                .find(|action| match action {
                    Action::CastSpell { card, choices, .. } => {
                        game.players[0].hand.iter().any(|held| {
                            held.id == *card && held.definition == cards::LIGHTNING_BOLT
                        }) && choices
                            .targets()
                            .iter()
                            .flat_map(crate::casting::TargetSelection::targets)
                            .any(|target| *target == Target::Player(PlayerId::Two))
                    }
                    _ => false,
                });
        let Some(cast) = cast else { break };
        game.apply(PlayerId::One, cast).expect("it is cast");
        settle(&mut game);
        drain_pending(&mut game);
    }

    assert_eq!(game.cards_drawn_this_turn[0], 1, "three crimes, one payout");
}
