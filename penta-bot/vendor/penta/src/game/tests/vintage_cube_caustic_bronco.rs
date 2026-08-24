//! Caustic Bronco: a two-drop that draws an extra card every attack, and a
//! saddle that decides who pays for it.

use super::*;

/// The Bronco and `friends` on the battlefield, with `library` on top.
fn staged(friends: &[CardDefinitionId], library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::One.index()].library.clear();
    // The library is a stack, so the last pushed is the top card.
    for (index, definition) in library.iter().rev().enumerate() {
        game.players[PlayerId::One.index()].library.push(card(
            92_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    for (index, definition) in friends.iter().enumerate() {
        game.battlefield.push(creature(
            92_100 + u32::try_from(index).expect("few creatures"),
            *definition,
            PlayerId::One,
        ));
    }
    let bronco = game
        .put_onto_battlefield(PlayerId::One, cards::CAUSTIC_BRONCO)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, bronco)
}

fn saddle_action(game: &Game, bronco: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One).into_iter().find(
        |action| matches!(action, Action::ActivateAbility { source, .. } if *source == bronco),
    )
}

/// Answers everything waiting by taking the first option offered.
fn settle(game: &mut Game) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .map(|option| option.id)
                .take(decision.minimum.max(1).min(decision.maximum))
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
}

fn attack(game: &mut Game, bronco: GameObjectId) {
    game.step = Step::DeclareAttackers;
    game.declare_attacker(bronco, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(game);
}

fn saddled(game: &Game, bronco: GameObjectId) -> bool {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == bronco)
        .expect("it is there")
        .saddled
}

/// Unsaddled, the card it reveals costs you its mana value.
#[test]
fn attacking_unsaddled_costs_you_the_mana_value() {
    let (mut game, bronco) = staged(&[], &[cards::SERRA_ANGEL]);
    let before = [
        game.players[PlayerId::One.index()].life,
        game.players[PlayerId::Two.index()].life,
    ];

    attack(&mut game, bronco);

    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::SERRA_ANGEL],
        "the top card is revealed into your hand",
    );
    assert_eq!(
        [
            game.players[PlayerId::One.index()].life,
            game.players[PlayerId::Two.index()].life,
        ],
        [before[0] - 5, before[1]],
        "a five-mana Angel costs you five",
    );
}

/// Saddled, the same reveal costs them instead.
#[test]
fn saddling_turns_the_drain_around() {
    let (mut game, bronco) = staged(
        &[cards::GRIZZLY_BEARS, cards::GRIZZLY_BEARS],
        &[cards::SERRA_ANGEL],
    );
    let before = [
        game.players[PlayerId::One.index()].life,
        game.players[PlayerId::Two.index()].life,
    ];

    let saddle = saddle_action(&game, bronco).expect("two bears are four power");
    game.apply(PlayerId::One, saddle).expect("it activates");
    settle(&mut game);
    assert!(saddled(&game, bronco));

    attack(&mut game, bronco);

    assert_eq!(
        [
            game.players[PlayerId::One.index()].life,
            game.players[PlayerId::Two.index()].life,
        ],
        [before[0], before[1] - 5],
        "saddled, the Angel costs them five",
    );
}

/// The saddle taps what pays for it, and only other creatures may.
#[test]
fn saddling_taps_the_creatures_that_paid() {
    let (mut game, bronco) = staged(&[cards::GRIZZLY_BEARS, cards::GRIZZLY_BEARS], &[]);

    let saddle = saddle_action(&game, bronco).expect("two bears are four power");
    game.apply(PlayerId::One, saddle).expect("it activates");
    settle(&mut game);

    assert!(
        game.battlefield
            .iter()
            .filter(|permanent| permanent.card.definition == cards::GRIZZLY_BEARS)
            .any(|permanent| permanent.tapped),
        "something was tapped to pay",
    );
    assert!(
        !game
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == bronco)
            .expect("it is there")
            .tapped,
        "the Mount itself is not what pays",
    );
}

/// Not enough power on the board is not an offer.
#[test]
fn one_small_creature_cannot_pay() {
    let (game, bronco) = staged(&[cards::SAVANNAH_LIONS], &[]);

    assert!(
        saddle_action(&game, bronco).is_none(),
        "two power is not three",
    );
}

/// Saddling is sorcery speed, and the saddle ends with the turn.
#[test]
fn it_saddles_only_as_a_sorcery_and_only_for_the_turn() {
    let (mut game, bronco) = staged(&[cards::GRIZZLY_BEARS, cards::GRIZZLY_BEARS], &[]);
    game.step = Step::Upkeep;
    assert!(
        saddle_action(&game, bronco).is_none(),
        "an upkeep is not a main phase",
    );

    game.step = Step::PrecombatMain;
    let saddle = saddle_action(&game, bronco).expect("a main phase is");
    game.apply(PlayerId::One, saddle).expect("it activates");
    settle(&mut game);
    assert!(saddled(&game, bronco));

    game.cleanup();
    assert!(!saddled(&game, bronco), "the saddle ends with the turn");
}
