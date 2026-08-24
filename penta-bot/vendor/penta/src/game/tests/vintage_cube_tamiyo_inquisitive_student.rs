//! Tamiyo, Inquisitive Student: one blue mana for a blocker that draws, and
//! a planeswalker on the turn the deck does what it was built to do.

use super::*;

/// Her on the battlefield since last turn, with `graveyard` behind her.
fn staged(graveyard: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    for (index, definition) in graveyard.iter().enumerate() {
        game.players[0].graveyard.push(card(
            97_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    let tamiyo = game
        .put_onto_battlefield(PlayerId::One, cards::TAMIYO_INQUISITIVE_STUDENT)
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
    (game, tamiyo)
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

/// The permanent she is, whichever face is up.
fn tamiyo_on_battlefield(game: &Game) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| {
            permanent.card.definition == ObjectKind::Card(cards::TAMIYO_INQUISITIVE_STUDENT)
        })
        .expect("she is on the battlefield")
}

fn is_planeswalker(game: &Game) -> bool {
    game.permanent_types(tamiyo_on_battlefield(game))
        .is_some_and(|types| types.contains(CardType::Planeswalker))
}

fn clues(game: &Game) -> usize {
    game.battlefield
        .iter()
        .filter(|permanent| game.effective_subtypes(permanent).contains(&"Clue"))
        .count()
}

/// Activates the back face's `index`th loyalty ability -- 0 is +2, 1 is -3,
/// 2 is -7 -- naming `wanted` wherever it asks for a target.
fn activate_loyalty(game: &mut Game, index: u8, wanted: Option<GameObjectId>) {
    let tamiyo = tamiyo_on_battlefield(game).card.id;
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
                *source == tamiyo
                    && *ability == AbilityId(index)
                    && wanted.is_none_or(|wanted| {
                        targets
                            .iter()
                            .any(|slot| slot.targets().contains(&Target::Card(wanted)))
                    })
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("loyalty ability {index} is activatable"));
    game.apply(PlayerId::One, action).expect("it activates");
    settle(game);
}

/// A 0/3 flier that makes a Clue when she attacks.
#[test]
fn she_investigates_when_she_attacks() {
    let (mut game, tamiyo) = staged(&[]);
    assert!(game.has_flying(tamiyo_on_battlefield(&game)), "flying");
    assert_eq!(game.power(tamiyo_on_battlefield(&game)), Some(0));
    assert_eq!(game.toughness(tamiyo_on_battlefield(&game)), Some(3));

    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.declare_attacker(tamiyo, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);

    assert_eq!(clues(&game), 1, "one Clue for the attack");
}

/// The third draw of the turn turns her over; the second does not.
#[test]
fn the_third_draw_turns_her_over() {
    let (mut game, _tamiyo) = staged(&[]);

    game.draw_cards(PlayerId::One, 2);
    settle(&mut game);
    assert!(!is_planeswalker(&game), "two draws is not three");

    game.draw_cards(PlayerId::One, 1);
    settle(&mut game);

    assert!(is_planeswalker(&game), "the third draw turned her over");
    assert_eq!(
        tamiyo_on_battlefield(&game).counters(CounterKind::Loyalty),
        2,
        "and she came back with her printed loyalty",
    );
}

/// The count is per turn: two draws this turn and one the next does not
/// reach three.
#[test]
fn the_count_resets_with_the_turn() {
    let (mut game, _tamiyo) = staged(&[]);
    game.draw_cards(PlayerId::One, 2);
    settle(&mut game);

    for _ in 0..40 {
        if game.turn > 9 && game.active_player == PlayerId::One {
            break;
        }
        game.advance_step();
        settle(&mut game);
    }
    // The draw step already took one this turn, so one more makes two.
    game.draw_cards(PlayerId::One, 1);
    settle(&mut game);

    assert!(!is_planeswalker(&game), "a new turn starts the count over");
}

/// Sets her loyalty, for the abilities she cannot pay for on arrival.
fn set_loyalty(game: &mut Game, loyalty: u16) {
    let tamiyo = tamiyo_on_battlefield(game).card.id;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == tamiyo)
    {
        permanent.set_counters(CounterKind::Loyalty, loyalty);
    }
}

/// Turns her over and hands priority back with the planeswalker up.
fn transformed(graveyard: &[CardDefinitionId]) -> Game {
    let (mut game, _tamiyo) = staged(graveyard);
    game.draw_cards(PlayerId::One, 3);
    settle(&mut game);
    game.players[0].hand.clear();
    assert!(is_planeswalker(&game), "she is the planeswalker now");
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game
}

/// +2 shrinks whatever attacks her controller, and keeps doing it on the
/// opponent's turn -- which is the turn it was played for.
#[test]
fn the_plus_two_shrinks_attackers_until_your_next_turn() {
    let mut game = transformed(&[]);
    activate_loyalty(&mut game, 0, None);
    assert_eq!(
        tamiyo_on_battlefield(&game).counters(CounterKind::Loyalty),
        4,
    );

    let bears = creature(97_500, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.active_player = PlayerId::Two;
    game.priority = PlayerId::Two;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.declare_attacker(bears_id, AttackDefender::Player(PlayerId::One));
    game.finish_declaring_attackers();
    settle(&mut game);

    let attacker = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears_id)
        .expect("the attacker is there");
    assert_eq!(game.power(attacker), Some(1), "-1/-0");
    assert_eq!(game.toughness(attacker), Some(2), "and toughness untouched");
}

/// −3 takes a spell back, and a green one pays a mana for the trouble.
#[test]
fn the_minus_three_returns_a_spell_and_a_green_one_pays() {
    let mut game = transformed(&[cards::GIANT_GROWTH]);
    set_loyalty(&mut game, 3);
    let growth = game.players[0]
        .graveyard
        .iter()
        .find(|card| card.definition == cards::GIANT_GROWTH)
        .expect("it is in the graveyard")
        .id;

    activate_loyalty(&mut game, 1, Some(growth));

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GIANT_GROWTH],
    );
    assert_eq!(
        game.players[0].mana_pool.total(),
        1,
        "a green card adds one mana of any colour",
    );
}

/// A card that is not green returns just the same, and pays nothing.
#[test]
fn a_blue_card_returns_without_the_rebate() {
    let mut game = transformed(&[cards::ANCESTRAL_RECALL]);
    set_loyalty(&mut game, 3);
    let recall = game.players[0]
        .graveyard
        .iter()
        .find(|card| card.definition == cards::ANCESTRAL_RECALL)
        .expect("it is in the graveyard")
        .id;

    activate_loyalty(&mut game, 1, Some(recall));

    assert_eq!(game.players[0].hand.len(), 1);
    assert_eq!(game.players[0].mana_pool.total(), 0, "not green, no mana");
}

/// −7 draws half the library rounded up and leaves an emblem behind.
#[test]
fn the_ultimate_draws_half_the_library_and_makes_an_emblem() {
    let mut game = transformed(&[]);
    set_loyalty(&mut game, 7);
    game.players[0].library.truncate(9);
    let library = game.players[0].library.len();

    activate_loyalty(&mut game, 2, None);

    assert_eq!(
        game.players[0].hand.len(),
        library.div_ceil(2),
        "nine halves up to five"
    );
    assert_eq!(game.emblems.len(), 1, "and the emblem stays");
}
