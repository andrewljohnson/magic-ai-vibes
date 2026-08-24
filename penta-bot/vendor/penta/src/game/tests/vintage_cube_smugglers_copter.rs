//! Smuggler's Copter: a 3/3 flier any one creature can turn on, which fixes
//! every draw it connects with.

use super::*;

/// The Copter on the battlefield, with `crew` creatures beside it.
fn staged(crew: &[CardDefinitionId]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let copter = game
        .put_onto_battlefield(PlayerId::One, cards::SMUGGLER_S_COPTER)
        .expect("cataloged");
    let mut ids = Vec::new();
    for definition in crew {
        ids.push(
            game.put_onto_battlefield(PlayerId::One, *definition)
                .expect("cataloged"),
        );
    }
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
        permanent.tapped = false;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, copter, ids)
}

/// Answers whatever is asked, taking a "you may" rather than declining it:
/// the loot is the half of the card worth watching.
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
                .find(|option| option.label == "Do it")
                .map_or_else(
                    || {
                        decision
                            .options
                            .iter()
                            .map(|option| option.id)
                            .take(decision.minimum.max(1))
                            .collect()
                    },
                    |option| vec![option.id],
                );
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

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

fn is_creature(game: &Game, id: GameObjectId) -> bool {
    game.permanent_types(permanent(game, id))
        .is_some_and(|types| types.contains(CardType::Creature))
}

/// Crews it by tapping whatever is offered.
fn crew(game: &mut Game, copter: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == copter),
        )
        .expect("crew is activatable");
    game.apply(PlayerId::One, action).expect("it crews");
    settle(game);
}

/// Uncrewed it is an artifact and nothing else: no power, no toughness, and
/// nothing to attack with.
#[test]
fn uncrewed_it_is_not_a_creature() {
    let (game, copter, _) = staged(&[cards::GRIZZLY_BEARS]);

    assert!(!is_creature(&game, copter));
    assert_eq!(game.power(permanent(&game, copter)), None);
}

/// Crewing makes it a 3/3 artifact creature with flying.
#[test]
fn crewing_makes_it_a_flying_three_three() {
    let (mut game, copter, crewers) = staged(&[cards::GRIZZLY_BEARS]);

    crew(&mut game, copter);

    assert!(is_creature(&game, copter));
    assert_eq!(game.power(permanent(&game, copter)), Some(3));
    assert_eq!(game.toughness(permanent(&game, copter)), Some(3));
    assert!(game.has_flying(permanent(&game, copter)));
    assert!(
        permanent(&game, crewers[0]).tapped,
        "and the crew is tapped for it",
    );
}

/// Crew 1: one power is enough, and a 1/1 is one power.
#[test]
fn one_power_is_enough() {
    let (mut game, copter, _) = staged(&[cards::SAVANNAH_LIONS]);

    crew(&mut game, copter);

    assert!(is_creature(&game, copter));
}

/// With nothing to tap it stays an artifact.
#[test]
fn it_cannot_crew_itself() {
    let (game, copter, _) = staged(&[]);

    assert!(
        !game.legal_actions(PlayerId::One).into_iter().any(
            |action| matches!(action, Action::ActivateAbility { source, .. } if source == copter)
        ),
        "a Vehicle is not a creature and cannot pay its own crew cost",
    );
}

/// Attacking loots: a card drawn and a card discarded.
#[test]
fn attacking_loots() {
    let (mut game, copter, _) = staged(&[cards::GRIZZLY_BEARS]);
    crew(&mut game, copter);
    game.players[0]
        .hand
        .push(card(99_000, cards::MOUNTAIN, PlayerId::One));
    let hand = game.players[0].hand.len();
    let library = game.players[0].library.len();

    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.declare_attacker(copter, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);

    assert_eq!(game.players[0].library.len(), library - 1, "one drawn");
    assert_eq!(game.players[0].hand.len(), hand, "and one discarded");
    assert_eq!(game.players[0].graveyard.len(), 1);
}

/// Blocking loots the same way, which is the other half of one clause.
#[test]
fn blocking_loots_too() {
    let (mut game, copter, _) = staged(&[cards::GRIZZLY_BEARS]);
    crew(&mut game, copter);
    game.players[0]
        .hand
        .push(card(99_100, cards::MOUNTAIN, PlayerId::One));
    let library = game.players[0].library.len();
    let attacker = creature(99_200, cards::SERRA_ANGEL, PlayerId::Two);
    let attacker_id = attacker.card.id;
    game.battlefield.push(attacker);
    drain_pending(&mut game);

    game.active_player = PlayerId::Two;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == attacker_id)
    {
        permanent.entered_controller_turn = 0;
    }
    game.declare_attacker(attacker_id, AttackDefender::Player(PlayerId::One));
    game.finish_declaring_attackers();
    settle(&mut game);
    game.step = Step::DeclareBlockers;
    game.declare_blocker(copter, attacker_id);
    game.finish_declaring_blockers();
    settle(&mut game);

    assert_eq!(game.players[0].library.len(), library - 1, "blocking loots");
}

/// Crewing lasts until end of turn: next turn it is an artifact again.
#[test]
fn the_crew_wears_off() {
    let (mut game, copter, _) = staged(&[cards::GRIZZLY_BEARS]);
    crew(&mut game, copter);
    assert!(is_creature(&game, copter));

    for _ in 0..40 {
        if game.turn > 9 {
            break;
        }
        game.advance_step();
        settle(&mut game);
    }

    assert!(!is_creature(&game, copter), "back to being an artifact");
}
