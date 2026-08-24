//! Thundertrap Trainer: two mana to dig four cards deep, or six for two
//! bodies and two digs.

use super::*;

/// Player One holding the Trainer, with `library` stacked so the last entry
/// is on top.
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
    let trainer = game
        .build_zone(PlayerId::One, &[cards::THUNDERTRAP_TRAINER])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = trainer.id;
    game.players[0].hand.push(trainer);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, id)
}

/// Answers whatever is asked, taking the card whose definition is `wanted`
/// when it is offered and nothing otherwise.
fn settle_taking(game: &mut Game, wanted: Option<CardDefinitionId>) {
    for _ in 0..32 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options: Vec<_> = decision
                .options
                .iter()
                .filter(|option| match (wanted, option.card) {
                    (Some(wanted), Some((_, ObjectCharacteristics::Card { definition, .. }))) => {
                        definition == wanted
                    }
                    _ => false,
                })
                .map(|option| option.id)
                .take(1)
                .collect();
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

fn cast_for(game: &mut Game, card: GameObjectId, offspring: bool) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } => *id == card && choices.costs().alternative().is_some() == offspring,
            _ => false,
        })
        .expect("that way of casting him is on offer");
    game.apply(PlayerId::One, action).expect("he is cast");
}

fn trainers(game: &Game) -> Vec<&Permanent> {
    game.battlefield
        .iter()
        .filter(|permanent| game.effective_subtypes(permanent).contains(&"Otter"))
        .collect()
}

/// Cast for two, he digs four deep and takes the noncreature nonland card.
#[test]
fn he_digs_four_for_a_spell() {
    let (mut game, trainer) = staged(&[
        cards::SERRA_ANGEL,
        cards::MOUNTAIN,
        cards::GRIZZLY_BEARS,
        cards::LIGHTNING_BOLT,
        cards::FOREST,
    ]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);

    cast_for(&mut game, trainer, false);
    settle_taking(&mut game, Some(cards::LIGHTNING_BOLT));

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::LIGHTNING_BOLT],
        "the instant among the four came to hand",
    );
    assert_eq!(
        game.players[0].library.len(),
        4,
        "the three he passed over went to the bottom, and one was never seen",
    );
    assert_eq!(trainers(&game).len(), 1, "and no token without offspring");
}

/// A creature among the four is not a legal choice.
#[test]
fn a_creature_is_not_what_he_looks_for() {
    let (mut game, trainer) = staged(&[
        cards::MOUNTAIN,
        cards::GRIZZLY_BEARS,
        cards::SERRA_ANGEL,
        cards::FOREST,
    ]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);

    cast_for(&mut game, trainer, false);
    settle_taking(&mut game, Some(cards::GRIZZLY_BEARS));

    assert!(
        game.players[0].hand.is_empty(),
        "nothing among them was a noncreature nonland card",
    );
}

/// Paid for with offspring, he brings a 1/1 copy of himself -- and the copy
/// digs too.
#[test]
fn offspring_makes_a_one_one_copy_that_digs() {
    let (mut game, trainer) = staged(&[
        cards::SERRA_ANGEL,
        cards::MOUNTAIN,
        cards::LIGHTNING_BOLT,
        cards::GRIZZLY_BEARS,
        cards::FOREST,
        cards::MOUNTAIN,
        cards::ISLAND,
        cards::SWAMP,
    ]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 6);

    cast_for(&mut game, trainer, true);
    settle_taking(&mut game, Some(cards::LIGHTNING_BOLT));

    let bodies = trainers(&game);
    assert_eq!(bodies.len(), 2, "the token copy arrived");
    let token = bodies
        .iter()
        .find(|permanent| game.power(permanent) == Some(1) && game.toughness(permanent) == Some(1))
        .expect("the copy is a 1/1");
    assert!(
        token.card.definition.is_token(),
        "and it is a token rather than the card",
    );
    assert!(
        bodies
            .iter()
            .any(|permanent| game.toughness(permanent) == Some(2)),
        "while the original is still a 1/2",
    );
    assert!(
        game.players[0]
            .hand
            .iter()
            .any(|card| card.definition == cards::LIGHTNING_BOLT),
        "and the digs happened",
    );
}

/// Without paying it, nothing is copied even though the trigger is printed.
#[test]
fn no_offspring_no_token() {
    let (mut game, trainer) = staged(&[cards::MOUNTAIN, cards::FOREST, cards::ISLAND]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 6);

    cast_for(&mut game, trainer, false);
    settle_taking(&mut game, None);

    assert_eq!(trainers(&game).len(), 1);
}
