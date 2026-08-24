//! Mishra's Bauble: a free artifact that replaces itself a turn later.

use super::*;

/// The Bauble on the battlefield, with cards in both libraries.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    game.players[1].library.clear();
    for (index, definition) in [cards::MOUNTAIN, cards::FOREST].into_iter().enumerate() {
        let id = 277_000 + u32::try_from(index).expect("two cards");
        game.players[0]
            .library
            .push(card(id, definition, PlayerId::One));
        let id = 277_100 + u32::try_from(index).expect("two cards");
        game.players[1]
            .library
            .push(card(id, definition, PlayerId::Two));
    }
    let bauble = game
        .put_onto_battlefield(PlayerId::One, cards::MISHRA_S_BAUBLE)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, bauble)
}

fn settle(game: &mut Game) {
    for _ in 0..32 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            // The look takes nothing, so the honest answer to it is an
            // empty selection rather than the first card offered.
            let options = decision
                .options
                .iter()
                .map(|option| option.id)
                .take(decision.minimum)
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

/// Cracks it, naming `at` as the player whose library is looked at.
fn crack(game: &mut Game, bauble: GameObjectId, at: PlayerId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == bauble
                    && targets
                        .iter()
                        .any(|slot| slot.targets().contains(&Target::Player(at)))
            }
            _ => false,
        })
        .expect("it can point at either player");
    game.apply(PlayerId::One, action).expect("it is activated");
    settle(game);
}

/// It costs nothing, sacrifices itself, and nothing moves in the library it
/// looked at.
#[test]
fn cracking_it_costs_the_bauble_and_moves_nothing() {
    let (mut game, bauble) = staged();

    crack(&mut game, bauble, PlayerId::Two);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bauble),
        "it sacrificed itself",
    );
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MISHRA_S_BAUBLE],
    );
    assert_eq!(
        game.players[1]
            .library
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MOUNTAIN, cards::FOREST],
        "the looking leaves the library as it found it",
    );
    assert!(game.players[0].hand.is_empty(), "and nothing is drawn yet");
}

/// The draw waits for the next upkeep, whoever's turn that is.
#[test]
fn the_card_comes_at_the_next_upkeep() {
    let (mut game, bauble) = staged();
    crack(&mut game, bauble, PlayerId::One);
    assert_eq!(
        game.installed_triggers.len(),
        1,
        "the delayed draw is waiting",
    );

    game.active_player = PlayerId::Two;
    game.step = Step::Upkeep;
    game.handle_upkeep_triggers();
    settle(&mut game);

    assert_eq!(
        game.players[0].hand.len(),
        1,
        "their upkeep is the next one",
    );
    assert!(
        game.installed_triggers.is_empty(),
        "and the listener is spent",
    );
}

/// One upkeep only: a second one draws nothing more.
#[test]
fn it_draws_once_and_no_more() {
    let (mut game, bauble) = staged();
    crack(&mut game, bauble, PlayerId::One);

    game.active_player = PlayerId::Two;
    game.step = Step::Upkeep;
    game.handle_upkeep_triggers();
    settle(&mut game);
    game.active_player = PlayerId::One;
    game.handle_upkeep_triggers();
    settle(&mut game);

    assert_eq!(game.players[0].hand.len(), 1, "one card, not two");
}

/// Either player's library may be the one looked at.
#[test]
fn it_can_look_at_your_own_library() {
    let (mut game, bauble) = staged();

    crack(&mut game, bauble, PlayerId::One);

    assert_eq!(
        game.players[0]
            .library
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MOUNTAIN, cards::FOREST],
        "your own library is left alone too",
    );
}
