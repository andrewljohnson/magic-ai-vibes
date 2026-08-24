//! Oath of Druids: two mana that puts something enormous onto the
//! battlefield for free, for the deck that lets the other player go first.

use super::*;

/// The Oath under Player One, with `library` stacked so the last entry is on
/// top of Player One's library.
fn staged(library: &[CardDefinitionId]) -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    game.players[0].graveyard.clear();
    for definition in library {
        let card = game
            .build_zone(PlayerId::One, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].library.push(card);
    }
    game.put_onto_battlefield(PlayerId::One, cards::OATH_OF_DRUIDS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.turn = 9;
    game
}

/// Runs `player`'s upkeep, taking every offer.
fn upkeep_of(game: &mut Game, player: PlayerId, accept: bool) {
    game.active_player = player;
    game.step = Step::Upkeep;
    game.priority = player;
    game.handle_upkeep_triggers();
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .filter(|option| {
                    if accept {
                        !option.label.contains("Decline")
                    } else {
                        option.label.contains("Decline")
                    }
                })
                .map(|option| option.id)
                .take(1)
                .collect::<Vec<_>>();
            let options = if options.len() < decision.minimum {
                decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum)
                    .collect()
            } else {
                options
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

fn on_battlefield(game: &Game, definition: CardDefinitionId) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.definition == definition)
}

/// Behind on creatures, the upkeep player digs to the first creature card:
/// it arrives, and everything above it is buried.
#[test]
fn being_behind_puts_a_creature_onto_the_battlefield() {
    let mut game = staged(&[cards::SERRA_ANGEL, cards::LIGHTNING_BOLT, cards::MOUNTAIN]);
    game.battlefield
        .push(creature(200_000, cards::GRIZZLY_BEARS, PlayerId::Two));

    upkeep_of(&mut game, PlayerId::One, true);

    assert!(
        on_battlefield(&game, cards::SERRA_ANGEL),
        "the Angel arrived"
    );
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MOUNTAIN, cards::LIGHTNING_BOLT],
        "and everything above it is buried",
    );
    assert!(game.players[0].library.is_empty());
}

/// Level on creatures, nothing happens: the condition is a comparison, not a
/// count of your own.
#[test]
fn being_level_does_nothing() {
    let mut game = staged(&[cards::SERRA_ANGEL, cards::MOUNTAIN]);
    game.battlefield
        .push(creature(200_100, cards::GRIZZLY_BEARS, PlayerId::Two));
    game.battlefield
        .push(creature(200_101, cards::GRIZZLY_BEARS, PlayerId::One));

    upkeep_of(&mut game, PlayerId::One, true);

    assert!(!on_battlefield(&game, cards::SERRA_ANGEL));
    assert_eq!(game.players[0].library.len(), 2, "nothing was revealed");
}

/// It is each player's upkeep, not the controller's: the other player digs
/// out of their own library when they are the one behind.
#[test]
fn the_other_players_upkeep_asks_about_them() {
    let mut game = staged(&[cards::MOUNTAIN]);
    game.players[1].library.clear();
    for definition in [cards::SERRA_ANGEL, cards::LIGHTNING_BOLT] {
        let card = game
            .build_zone(PlayerId::Two, &[definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[1].library.push(card);
    }
    game.battlefield
        .push(creature(200_200, cards::GRIZZLY_BEARS, PlayerId::One));

    upkeep_of(&mut game, PlayerId::Two, true);

    let angel = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::SERRA_ANGEL)
        .expect("their Angel arrived");
    assert_eq!(angel.controller, PlayerId::Two, "under their control");
    assert_eq!(game.players[1].graveyard.len(), 1);
    assert_eq!(
        game.players[0].library.len(),
        1,
        "and the Oath's controller revealed nothing",
    );
}

/// The dig is optional.
#[test]
fn declining_leaves_the_library_alone() {
    let mut game = staged(&[cards::SERRA_ANGEL, cards::MOUNTAIN]);
    game.battlefield
        .push(creature(200_300, cards::GRIZZLY_BEARS, PlayerId::Two));

    upkeep_of(&mut game, PlayerId::One, false);

    assert!(!on_battlefield(&game, cards::SERRA_ANGEL));
    assert_eq!(game.players[0].library.len(), 2);
}
