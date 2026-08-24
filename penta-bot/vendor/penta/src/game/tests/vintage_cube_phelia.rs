//! Phelia, Exuberant Shepherd: a two-mana attacker that takes something
//! away for a turn, and grows every time what it took was yours.

use super::*;

/// Phelia on the battlefield beside `board`, ready to attack.
fn staged(board: &[(CardDefinitionId, PlayerId)]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    let mut ids = Vec::new();
    for (index, (definition, controller)) in board.iter().enumerate() {
        let permanent = creature(
            94_000 + u32::try_from(index).expect("few permanents"),
            *definition,
            *controller,
        );
        ids.push(permanent.card.id);
        game.battlefield.push(permanent);
    }
    let phelia = game
        .put_onto_battlefield(PlayerId::One, cards::PHELIA_EXUBERANT_SHEPHERD)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, phelia, ids)
}

/// Attacks and points the trigger at `taken`, or at nothing when `None`.
fn attack_taking(game: &mut Game, phelia: GameObjectId, taken: Option<GameObjectId>) {
    game.step = Step::DeclareAttackers;
    game.declare_attacker(phelia, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = match taken {
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

/// Runs the end step, where the delayed trigger gives it back.
fn end_step(game: &mut Game) {
    game.step = Step::End;
    game.begin_step_triggers();
    drain_pending(game);
}

fn on_battlefield(game: &Game, definition: CardDefinitionId) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.definition == definition)
}

fn counters(game: &Game, phelia: GameObjectId) -> u16 {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == phelia)
        .expect("she is there")
        .counters(CounterKind::PlusOnePlusOne)
}

/// Taking an opponent's permanent removes it for the turn and gives it back
/// at end of turn, with nothing for Phelia.
#[test]
fn taking_theirs_gives_it_back_and_grows_nothing() {
    let (mut game, phelia, ids) = staged(&[(cards::SERRA_ANGEL, PlayerId::Two)]);
    let angel = ids[0];

    attack_taking(&mut game, phelia, Some(angel));
    assert!(
        !on_battlefield(&game, cards::SERRA_ANGEL),
        "it is exiled for the turn",
    );

    end_step(&mut game);

    assert!(on_battlefield(&game, cards::SERRA_ANGEL), "and comes back");
    assert_eq!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.definition == cards::SERRA_ANGEL)
            .expect("it is back")
            .controller,
        PlayerId::Two,
        "under its owner's control",
    );
    assert_eq!(counters(&game, phelia), 0, "it was not yours to blink");
}

/// Blinking your own permanent grows her.
#[test]
fn blinking_your_own_grows_her() {
    let (mut game, phelia, ids) = staged(&[(cards::GRIZZLY_BEARS, PlayerId::One)]);
    let bears = ids[0];

    attack_taking(&mut game, phelia, Some(bears));
    end_step(&mut game);

    assert!(on_battlefield(&game, cards::GRIZZLY_BEARS));
    assert_eq!(
        counters(&game, phelia),
        1,
        "your own blink is worth a counter"
    );
}

/// "Up to one": she may take nothing, and then nothing comes back.
#[test]
fn she_may_take_nothing() {
    let (mut game, phelia, _) = staged(&[(cards::GRIZZLY_BEARS, PlayerId::One)]);

    attack_taking(&mut game, phelia, None);
    end_step(&mut game);

    assert!(on_battlefield(&game, cards::GRIZZLY_BEARS));
    assert_eq!(counters(&game, phelia), 0);
}

/// "Other": she cannot exile herself.
#[test]
fn she_cannot_take_herself() {
    let (mut game, phelia, _) = staged(&[(cards::GRIZZLY_BEARS, PlayerId::One)]);
    game.step = Step::DeclareAttackers;
    game.declare_attacker(phelia, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    let priority = game.priority;
    game.apply(priority, Action::PassPriority).expect("legal");

    let decision = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the trigger asks what to take");
    assert!(
        decision
            .options
            .iter()
            .all(|option| option.card.is_none_or(|(object, _)| object != phelia)),
        "she is not among her own targets",
    );
}
