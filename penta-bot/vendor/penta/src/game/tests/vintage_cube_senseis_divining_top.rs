//! Sensei's Divining Top: one mana that fixes every draw for the rest of
//! the game, and never really leaves.

use super::*;

/// The Top on the battlefield, with `library` stacked so the last entry is
/// on top.
fn staged(library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for definition in library {
        let card = game
            .build_zone(PlayerId::One, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].library.push(card);
    }
    let top = game
        .put_onto_battlefield(PlayerId::One, cards::SENSEIS_DIVINING_TOP)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, top)
}

/// Answers whatever is asked, naming the cards whose definitions are listed
/// in that order.
fn settle_ordering(game: &mut Game, order: &[CardDefinitionId]) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let mut options = Vec::new();
            for wanted in order {
                if let Some(option) = decision.options.iter().find(|option| {
                    !options.contains(&option.id)
                        && matches!(
                            option.card,
                            Some((_, ObjectCharacteristics::Card { definition, .. }))
                                if definition == *wanted
                        )
                }) {
                    options.push(option.id);
                }
            }
            if options.len() < decision.minimum {
                options = decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum)
                    .collect();
            }
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

fn activate(game: &mut Game, top: GameObjectId, ability: u8) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source,
                ability: AbilityOrigin::Printed { ability: id, .. },
                ..
            } => *source == top && *id == AbilityId(ability),
            _ => false,
        })
        .unwrap_or_else(|| panic!("ability {ability} is activatable"));
    game.apply(PlayerId::One, action).expect("it activates");
}

/// Library order, top last, which is how the engine stacks it.
fn library(game: &Game) -> Vec<CardDefinitionId> {
    game.players[0]
        .library
        .iter()
        .map(|card| card.definition)
        .collect()
}

/// The mana ability rearranges the top three and leaves the library the
/// same size.
#[test]
fn the_look_rearranges_the_top_three() {
    let (mut game, top) = staged(&[
        cards::FOREST,
        cards::SERRA_ANGEL,
        cards::GRIZZLY_BEARS,
        cards::LIGHTNING_BOLT,
    ]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    activate(&mut game, top, 0);
    // Named first goes on top, so the Bolt ends up under the Angel.
    settle_ordering(
        &mut game,
        &[
            cards::SERRA_ANGEL,
            cards::LIGHTNING_BOLT,
            cards::GRIZZLY_BEARS,
        ],
    );

    assert_eq!(game.players[0].library.len(), 4, "nothing left the library");
    assert_eq!(
        library(&game).last(),
        Some(&cards::SERRA_ANGEL),
        "the card named first is the one on top",
    );
    assert!(game.players[0].hand.is_empty(), "and nothing was drawn");
}

/// Tapping draws the top card and puts the Top back on the library.
#[test]
fn tapping_draws_and_puts_it_back() {
    let (mut game, top) = staged(&[cards::FOREST, cards::LIGHTNING_BOLT]);

    activate(&mut game, top, 1);
    settle_ordering(&mut game, &[]);

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::LIGHTNING_BOLT],
        "the card it had just arranged",
    );
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == top),
        "and the Top left the battlefield",
    );
    assert_eq!(
        library(&game).last().copied(),
        Some(cards::SENSEIS_DIVINING_TOP),
        "onto the library, on top",
    );
}

/// The draw happens before it goes, so an empty library is still a draw.
#[test]
fn the_draw_comes_before_the_trip_back() {
    let (mut game, top) = staged(&[cards::LIGHTNING_BOLT]);

    activate(&mut game, top, 1);
    settle_ordering(&mut game, &[]);

    assert_eq!(game.players[0].hand.len(), 1);
    assert_eq!(
        library(&game),
        vec![cards::SENSEIS_DIVINING_TOP],
        "it is the whole library now",
    );
}
