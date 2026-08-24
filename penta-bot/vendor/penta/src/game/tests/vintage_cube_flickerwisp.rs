//! Flickerwisp: a 3/1 flier that takes something away for a turn and gives
//! it back at the next end step, whether or not he is still there.

use super::*;

fn staged() -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game
}

/// Answers every pending decision, naming `wanted` where it is offered.
fn settle_naming(game: &mut Game, wanted: Option<GameObjectId>) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = match wanted {
                Some(wanted) => decision
                    .options
                    .iter()
                    .filter(|option| option.card.is_some_and(|(object, _)| object == wanted))
                    .map(|option| option.id)
                    .take(1)
                    .collect(),
                None => decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum)
                    .collect(),
            };
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the offered choice is legal");
            continue;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
    game.check_state_based_actions();
}

/// Puts the Wisp onto the battlefield and points his arrival at `target`.
fn arrive(game: &mut Game, target: GameObjectId) -> GameObjectId {
    let wisp = game
        .put_onto_battlefield(PlayerId::One, cards::FLICKERWISP)
        .expect("cataloged");
    settle_naming(game, Some(target));
    wisp
}

/// Runs the end step, where the delayed trigger gives the card back.
fn end_step(game: &mut Game) {
    game.step = Step::End;
    game.begin_step_triggers();
    settle_naming(game, None);
}

fn on_battlefield(game: &Game, definition: CardDefinitionId) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.definition == definition)
}

/// He answers something for a turn, and it comes back under its owner's
/// control at the end step.
#[test]
fn he_exiles_a_permanent_until_the_end_step() {
    let mut game = staged();
    let bears = creature(140_000, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);

    arrive(&mut game, bears_id);

    assert!(
        !on_battlefield(&game, cards::GRIZZLY_BEARS),
        "the target is in exile",
    );
    assert_eq!(game.players[1].exile.len(), 1);

    end_step(&mut game);

    let returned = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::GRIZZLY_BEARS)
        .expect("it comes back");
    assert_eq!(
        returned.controller,
        PlayerId::Two,
        "under its owner's control",
    );
    assert!(game.players[1].exile.is_empty());
}

/// A land is a legal target, which is the half of him no other white
/// flicker has.
#[test]
fn he_may_take_a_land() {
    let mut game = staged();
    let mountain = game
        .put_onto_battlefield(PlayerId::Two, cards::MOUNTAIN)
        .expect("cataloged");
    drain_pending(&mut game);

    arrive(&mut game, mountain);

    assert!(!on_battlefield(&game, cards::MOUNTAIN));

    end_step(&mut game);

    assert!(on_battlefield(&game, cards::MOUNTAIN));
}

/// The return is a delayed trigger of its own: answering the Wisp in the
/// meantime does not keep what he took.
#[test]
fn the_card_returns_even_if_he_dies() {
    let mut game = staged();
    let bears = creature(140_200, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);

    let wisp = arrive(&mut game, bears_id);
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == wisp)
    {
        permanent.damage = 1;
    }
    settle_naming(&mut game, None);
    assert!(!on_battlefield(&game, cards::FLICKERWISP), "he is gone");

    end_step(&mut game);

    assert!(on_battlefield(&game, cards::GRIZZLY_BEARS));
}

/// "Another": he is not among the permanents his own arrival may name.
#[test]
fn he_cannot_name_himself() {
    let mut game = staged();
    let bears = creature(140_300, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let wisp = game
        .put_onto_battlefield(PlayerId::One, cards::FLICKERWISP)
        .expect("cataloged");

    let decision = loop {
        if let Some(pending) = game.pending_decisions.first() {
            break pending.observation.clone();
        }
        let priority = game.priority;
        game.apply(priority, Action::PassPriority)
            .expect("the arrival is waiting to be put on the stack");
    };
    let named: Vec<_> = decision
        .options
        .iter()
        .filter_map(|option| option.card.map(|(object, _)| object))
        .collect();
    assert!(named.contains(&bears_id));
    assert!(!named.contains(&wisp), "he is not another permanent");
}
