//! Scythecat Cub: a land drop is a counter, and the second land of the turn
//! is all of them at once.

use super::*;

/// The Cub on the battlefield since last turn, with `others` beside her.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.turns_started = [5, 5];
    game.turn = 9;
    let cub = game
        .put_onto_battlefield(PlayerId::One, cards::SCYTHECAT_CUB)
        .expect("cataloged");
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, cub)
}

/// Answers every pending decision, naming `wanted` where it is offered.
fn settle_naming(game: &mut Game, wanted: GameObjectId) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .filter(|option| option.card.is_some_and(|(object, _)| object == wanted))
                .map(|option| option.id)
                .take(1)
                .collect();
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

/// Drops a land and points the landfall trigger at `wanted`.
fn drop_a_land(game: &mut Game, wanted: GameObjectId) {
    game.put_onto_battlefield(PlayerId::One, cards::MOUNTAIN)
        .expect("cataloged");
    settle_naming(game, wanted);
}

fn counters(game: &Game, object: GameObjectId) -> u16 {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == object)
        .expect("it is on the battlefield")
        .counters(CounterKind::PlusOnePlusOne)
}

/// She has trample, which is what makes the counters worth putting on her.
#[test]
fn she_tramples() {
    let (game, cub) = staged();
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == cub)
        .expect("she is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Trample));
}

/// The first land of the turn is one counter; the second doubles what is
/// there; a third goes back to one.
#[test]
fn the_second_land_of_the_turn_doubles_instead() {
    let (mut game, cub) = staged();

    drop_a_land(&mut game, cub);
    assert_eq!(counters(&game, cub), 1, "the first land is a counter");

    drop_a_land(&mut game, cub);
    assert_eq!(counters(&game, cub), 2, "the second doubles the one there");

    drop_a_land(&mut game, cub);
    assert_eq!(
        counters(&game, cub),
        3,
        "a third is not the second time, so it is one counter again",
    );
}

/// The doubling reads what the creature has, not what this ability put
/// there: counters from anywhere double.
#[test]
fn the_doubling_reads_every_counter() {
    let (mut game, cub) = staged();
    let mut bears = creature(160_000, cards::GRIZZLY_BEARS, PlayerId::One);
    bears.add_counters(CounterKind::PlusOnePlusOne, 4);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);

    drop_a_land(&mut game, cub);
    drop_a_land(&mut game, bears_id);

    assert_eq!(counters(&game, bears_id), 8, "four doubled, not five");
    assert_eq!(counters(&game, cub), 1);
}

/// The count is per turn: next turn the first land is a counter again.
#[test]
fn the_count_resets_with_the_turn() {
    let (mut game, cub) = staged();

    drop_a_land(&mut game, cub);
    drop_a_land(&mut game, cub);
    assert_eq!(counters(&game, cub), 2);

    game.commit_next_turn(PlayerId::Two, Vec::new());
    game.commit_next_turn(PlayerId::One, Vec::new());
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;

    drop_a_land(&mut game, cub);
    assert_eq!(
        counters(&game, cub),
        3,
        "a fresh turn starts the count over"
    );
}
