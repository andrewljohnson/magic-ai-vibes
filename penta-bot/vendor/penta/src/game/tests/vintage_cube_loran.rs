//! Loran of the Third Path: an artifact answer stapled to a body, and a
//! symmetrical draw that is only symmetrical on paper.

use super::*;

fn staged(board: &[(CardDefinitionId, PlayerId)]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    game.players[1].library.clear();
    for index in 0..4 {
        game.players[0]
            .library
            .push(card(103_000 + index, cards::GRIZZLY_BEARS, PlayerId::One));
        game.players[1]
            .library
            .push(card(103_100 + index, cards::SAVANNAH_LIONS, PlayerId::Two));
    }
    let mut ids = Vec::new();
    for (index, (definition, controller)) in board.iter().enumerate() {
        let permanent = creature(
            103_200 + u32::try_from(index).expect("few permanents"),
            *definition,
            *controller,
        );
        ids.push(permanent.card.id);
        game.battlefield.push(permanent);
    }
    let loran = game
        .put_onto_battlefield(PlayerId::One, cards::LORAN_OF_THE_THIRD_PATH)
        .expect("cataloged");
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, loran, ids)
}

/// Answers the entry trigger by naming `target`, or nobody when `None`.
fn settle(game: &mut Game, target: Option<GameObjectId>) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = match target {
                Some(wanted) => decision
                    .options
                    .iter()
                    .filter(|option| option.card.is_some_and(|(object, _)| object == wanted))
                    .map(|option| option.id)
                    .take(1)
                    .collect(),
                None => Vec::new(),
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
}

/// Arriving answers an artifact.
#[test]
fn arriving_destroys_an_artifact() {
    let (mut game, _, ids) = staged(&[(cards::MOX_JET, PlayerId::Two)]);
    let mox = ids[0];

    settle(&mut game, Some(mox));

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != mox),
    );
}

/// "Up to one": with nothing worth answering it simply arrives.
#[test]
fn it_may_answer_nothing() {
    let (mut game, loran, _) = staged(&[(cards::MOX_JET, PlayerId::Two)]);

    settle(&mut game, None);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::MOX_JET),
        "nothing was named",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == loran),
    );
}

/// Tapping draws for both, and leaves it tapped -- but vigilance means
/// attacking never costs the draw.
#[test]
fn tapping_draws_for_both_players() {
    let (mut game, loran, _) = staged(&[]);
    settle(&mut game, None);
    assert!(
        game.permanent_has_executable_keyword(
            game.battlefield
                .iter()
                .find(|permanent| permanent.card.id == loran)
                .expect("she is there"),
            KeywordAbility::Vigilance,
        )
    );

    let draw = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == loran))
        .expect("tapping is the whole cost");
    game.apply(PlayerId::One, draw).expect("it activates");
    settle(&mut game, None);

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GRIZZLY_BEARS],
    );
    assert_eq!(
        game.players[1]
            .hand
            .iter()
            .filter(|card| card.definition == cards::SAVANNAH_LIONS)
            .count(),
        1,
        "and the opponent it named draws too",
    );
}
