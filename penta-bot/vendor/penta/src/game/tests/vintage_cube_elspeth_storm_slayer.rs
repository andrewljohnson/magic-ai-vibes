//! Elspeth, Storm Slayer: five mana whose first line is worth more than the
//! three below it.

use super::*;

/// Her on the battlefield since last turn, with `others` beside her.
fn staged(others: &[CardDefinitionId]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let elspeth = game
        .put_onto_battlefield(PlayerId::One, cards::ELSPETH_STORM_SLAYER)
        .expect("cataloged");
    let mut ids = Vec::new();
    for definition in others {
        ids.push(
            game.put_onto_battlefield(PlayerId::One, *definition)
                .expect("cataloged"),
        );
    }
    drain_pending(&mut game);
    settle(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, elspeth, ids)
}

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
                .map(|option| option.id)
                .take(decision.minimum.max(1))
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

/// Activates her `index`th loyalty ability, naming `wanted` if it asks.
fn activate(game: &mut Game, elspeth: GameObjectId, index: u8, wanted: Option<GameObjectId>) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source,
                ability: AbilityOrigin::Printed { ability, .. },
                targets,
                ..
            } => {
                *source == elspeth
                    && *ability == AbilityId(index)
                    && wanted.is_none_or(|wanted| {
                        targets
                            .iter()
                            .any(|slot| slot.targets().contains(&Target::Permanent(wanted)))
                    })
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("loyalty ability {index} is activatable"));
    game.apply(PlayerId::One, action).expect("it activates");
    settle(game);
}

fn tokens(game: &Game) -> usize {
    game.battlefield
        .iter()
        .filter(|permanent| permanent.card.definition == ObjectKind::Token)
        .count()
}

/// Her plus makes one Soldier, and her own static makes it two.
#[test]
fn her_plus_makes_two_soldiers() {
    let (mut game, elspeth, _) = staged(&[]);

    activate(&mut game, elspeth, 1, None);

    assert_eq!(tokens(&game), 2, "one printed, doubled to two");
}

/// The doubling is not hers alone: anything that makes a token under her
/// controller makes twice as many.
#[test]
fn it_doubles_somebody_elses_tokens() {
    let (mut game, _elspeth, _) = staged(&[]);
    let before = tokens(&game);

    game.put_onto_battlefield(PlayerId::One, cards::AJANI_NACATL_PARIAH)
        .expect("cataloged");
    drain_pending(&mut game);
    settle(&mut game);

    assert_eq!(tokens(&game) - before, 2, "Ajani's one Cat arrives as two");
}

/// The doubling only helps its own controller.
#[test]
fn it_does_not_double_their_tokens() {
    let (mut game, _elspeth, _) = staged(&[]);
    let before = tokens(&game);

    game.put_onto_battlefield(PlayerId::Two, cards::AJANI_NACATL_PARIAH)
        .expect("cataloged");
    drain_pending(&mut game);
    settle(&mut game);

    assert_eq!(tokens(&game) - before, 1, "their Cat is one Cat");
}

/// Her zero grows everything and hands it flying.
#[test]
fn her_zero_grows_the_team_and_gives_it_flying() {
    let (mut game, elspeth, others) = staged(&[cards::GRIZZLY_BEARS]);
    let bears = others[0];

    activate(&mut game, elspeth, 2, None);

    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("it is there");
    assert_eq!(permanent.counters(CounterKind::PlusOnePlusOne), 1);
    assert_eq!(game.power(permanent), Some(3));
    assert!(game.has_flying(permanent), "and it flies");
}

/// Her minus kills something big.
#[test]
fn her_minus_kills_a_big_creature() {
    let (mut game, elspeth, _) = staged(&[]);
    let angel = game
        .put_onto_battlefield(PlayerId::Two, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);

    activate(&mut game, elspeth, 3, Some(angel));

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == angel),
        "five mana value is three or greater",
    );
}

/// And leaves a small one alone: it is not a legal target at all.
#[test]
fn her_minus_cannot_name_a_small_creature() {
    let (mut game, elspeth, _) = staged(&[]);
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);

    assert!(
        !game.legal_actions(PlayerId::One).into_iter().any(|action| {
            matches!(
                action,
                Action::ActivateAbility { source, targets, .. }
                    if source == elspeth
                        && targets
                            .iter()
                            .any(|slot| slot.targets().contains(&Target::Permanent(bears)))
            )
        }),
        "a two-drop is not mana value 3 or greater",
    );
}
