//! Subtlety: an evoked Elemental that answers a creature spell by putting it
//! back in its owner's library, at the end they choose.

use super::*;

/// Player Two with a Serra Angel on the stack, and Player One holding a
/// Subtlety plus a blue card to evoke it with.
fn staged() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[1].hand.clear();
    let angel = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let angel_id = angel.id;
    game.players[1].hand.push(angel);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::White, 5);
    // A creature spell needs its controller's main phase; the Subtlety that
    // answers it has flash and does not.
    game.active_player = PlayerId::Two;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::Two;
    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == angel_id))
        .expect("five mana buys an Angel");
    game.apply(PlayerId::Two, cast)
        .expect("it goes on the stack");
    let on_stack = game.stack.last().expect("the Angel is there").id;

    game.players[0].hand.clear();
    for definition in [cards::SUBTLETY, cards::ANCESTRAL_RECALL] {
        let card = game
            .build_zone(PlayerId::One, &[definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].hand.push(card);
    }
    let subtlety = game.players[0]
        .hand
        .iter()
        .find(|card| card.definition == cards::SUBTLETY)
        .expect("it is in hand")
        .id;
    game.priority = PlayerId::One;
    (game, subtlety, on_stack)
}

/// Evokes the Subtlety at `target`, answering the library-end choice with
/// `bottom`.
fn evoke(game: &mut Game, subtlety: GameObjectId, target: Option<GameObjectId>, bottom: bool) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == subtlety && choices.costs().alternative().is_some()
            }
            _ => false,
        })
        .expect("a blue card in hand pays for the evoke");
    game.apply(PlayerId::One, cast).expect("it is castable");
    settle(game, target, bottom);
}

fn settle(game: &mut Game, target: Option<GameObjectId>, bottom: bool) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = if decision.options.iter().any(|option| option.label == "Top") {
                let wanted = if bottom { "Bottom" } else { "Top" };
                decision
                    .options
                    .iter()
                    .find(|option| option.label == wanted)
                    .map(|option| vec![option.id])
                    .unwrap_or_default()
            } else if target.is_some() {
                decision
                    .options
                    .iter()
                    .take(decision.minimum.max(1))
                    .map(|option| option.id)
                    .collect()
            } else {
                decision
                    .options
                    .iter()
                    .take(decision.minimum)
                    .map(|option| option.id)
                    .collect()
            };
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
            return;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            return;
        }
    }
}

/// Chosen top, the Angel is the next card its owner draws.
#[test]
fn the_owner_may_put_it_on_top() {
    let (mut game, subtlety, angel) = staged();
    evoke(&mut game, subtlety, Some(angel), false);

    assert_eq!(
        game.players[1].library.last().map(|card| card.definition),
        Some(cards::SERRA_ANGEL),
        "top of their library",
    );
    assert!(game.stack.is_empty(), "and off the stack");
}

/// Chosen bottom, it is the last card in the deck.
#[test]
fn the_owner_may_put_it_on_the_bottom() {
    let (mut game, subtlety, angel) = staged();
    evoke(&mut game, subtlety, Some(angel), true);

    assert_eq!(
        game.players[1].library.first().map(|card| card.definition),
        Some(cards::SERRA_ANGEL),
        "bottom of their library",
    );
}

/// The choice belongs to the spell's owner, not to whoever evoked it.
#[test]
fn the_owner_is_the_one_asked() {
    let (mut game, subtlety, _) = staged();
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == subtlety && choices.costs().alternative().is_some()
            }
            _ => false,
        })
        .expect("a blue card in hand pays for the evoke");
    game.apply(PlayerId::One, cast).expect("it is castable");

    // Answer everything up to the library-end question, then check who is
    // being asked it.
    for _ in 0..12 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            if decision.options.iter().any(|option| option.label == "Top") {
                assert_eq!(
                    decision.player,
                    PlayerId::Two,
                    "the Angel's owner chooses, not the Elemental's controller",
                );
                return;
            }
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options: decision
                        .options
                        .iter()
                        .take(decision.minimum.max(1))
                        .map(|option| option.id)
                        .collect(),
                },
            )
            .expect("the decision accepts what it offered");
            continue;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
    panic!("the library-end question was never asked");
}

/// Evoked, it sacrifices itself; the answer still happened.
#[test]
fn an_evoked_subtlety_does_not_stay() {
    let (mut game, subtlety, angel) = staged();
    evoke(&mut game, subtlety, Some(angel), false);
    game.check_state_based_actions();

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SUBTLETY),
        "evoke's sacrifice comes due once it has arrived",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::ANCESTRAL_RECALL),
        "and the blue card it cost is in exile",
    );
}

/// Hard cast, the 3/3 stays.
#[test]
fn a_hard_cast_subtlety_stays() {
    let (mut game, subtlety, angel) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 4);
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == subtlety && choices.costs().alternative().is_none()
            }
            _ => false,
        })
        .expect("four mana buys it outright");
    game.apply(PlayerId::One, cast).expect("it is castable");
    settle(&mut game, Some(angel), false);
    game.check_state_based_actions();

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SUBTLETY),
        "nothing sacrifices a Subtlety that was paid for",
    );
}
