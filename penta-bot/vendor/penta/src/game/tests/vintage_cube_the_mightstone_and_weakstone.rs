//! The Mightstone and Weakstone: two cards or a dead creature on the way in,
//! and two mana a turn afterwards that artifacts alone may be cast with.

use super::*;

/// The stone on the battlefield, with a library to draw from.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for (index, definition) in [cards::MOUNTAIN, cards::FOREST, cards::PLAINS]
        .into_iter()
        .enumerate()
    {
        let id = 276_000 + u32::try_from(index).expect("three cards");
        game.players[0]
            .library
            .push(card(id, definition, PlayerId::One));
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    let stone = game
        .put_onto_battlefield(PlayerId::One, cards::THE_MIGHTSTONE_AND_WEAKSTONE)
        .expect("cataloged");
    (game, stone)
}

/// Answers whatever is asked, taking the option whose label starts with
/// `wanted` where there is one and the first otherwise.
fn settle(game: &mut Game, wanted: &str) {
    for _ in 0..32 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options: Vec<_> = decision
                .options
                .iter()
                .find(|option| option.label.starts_with(wanted))
                .map(|option| option.id)
                .into_iter()
                .collect();
            let options = if options.len() < decision.minimum.max(1) {
                decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum.max(1))
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

fn tap_for_mana(game: &mut Game, stone: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateManaAbility { source, .. } => *source == stone,
            _ => false,
        })
        .expect("it taps for mana");
    game.apply(PlayerId::One, action).expect("it activates");
}

fn can_cast(game: &Game, spell: GameObjectId) -> bool {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .any(|action| matches!(action, Action::CastSpell { card, .. } if card == spell))
}

/// The first mode draws two.
#[test]
fn the_first_mode_draws_two() {
    let (mut game, _stone) = staged();

    settle(&mut game, "Draw two");

    assert_eq!(game.players[0].hand.len(), 2);
    assert_eq!(game.players[0].library.len(), 1);
}

/// The second mode kills what it names.
#[test]
fn the_second_mode_shrinks_a_creature() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.put_onto_battlefield(PlayerId::Two, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.put_onto_battlefield(PlayerId::One, cards::THE_MIGHTSTONE_AND_WEAKSTONE)
        .expect("cataloged");

    settle(&mut game, "Target creature");

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == ObjectKind::Card(cards::SERRA_ANGEL)),
        "a 4/4 does not survive -5/-5",
    );
}

/// It taps for two colourless.
#[test]
fn it_taps_for_two() {
    let (mut game, stone) = staged();
    settle(&mut game, "Draw two");

    tap_for_mana(&mut game, stone);

    assert_eq!(game.players[0].mana_pool.total(), 2);
    assert!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == stone)
            .expect("still there")
            .tapped,
    );
}

/// Its mana casts an artifact spell.
#[test]
fn its_mana_casts_an_artifact() {
    let (mut game, stone) = staged();
    settle(&mut game, "Draw two");
    let map = game
        .build_zone(PlayerId::One, &[cards::EXPEDITION_MAP])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let map_id = map.id;
    game.players[0].hand.push(map);

    assert!(
        can_cast(&game, map_id),
        "the stone is the only mana around, and an artifact is what it is for",
    );

    tap_for_mana(&mut game, stone);
    assert!(
        can_cast(&game, map_id),
        "and the same is true once the mana is in the pool",
    );
}

/// And it pays for an activated ability, which is where a Powerstone's
/// restriction differs from "spend only to cast artifact spells".
#[test]
fn its_mana_pays_for_an_ability() {
    let (mut game, stone) = staged();
    settle(&mut game, "Draw two");
    let key = game
        .put_onto_battlefield(PlayerId::One, cards::MANIFOLD_KEY)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    tap_for_mana(&mut game, stone);

    let activation = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == key
                    && targets
                        .iter()
                        .any(|slot| slot.targets().contains(&Target::Permanent(stone)))
            }
            _ => false,
        })
        .expect("the Key's ability is payable with Powerstone mana");
    game.apply(PlayerId::One, activation)
        .expect("it is activated");
    settle(&mut game, "");

    assert!(
        !game
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == stone)
            .expect("still there")
            .tapped,
        "the Key untapped it",
    );
}

/// What it cannot do is cast a spell that is not an artifact, even with the
/// colours for it in the pool beside it.
#[test]
fn its_mana_cannot_cast_a_nonartifact() {
    let (mut game, stone) = staged();
    settle(&mut game, "Draw two");
    let angel = game
        .build_zone(PlayerId::One, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let angel_id = angel.id;
    game.players[0].hand.push(angel);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 3);
    tap_for_mana(&mut game, stone);

    assert_eq!(game.players[0].mana_pool.total(), 5, "five for a five-drop");
    assert!(
        !can_cast(&game, angel_id),
        "two of those five may not cast it, and it needs all five",
    );

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    assert!(
        can_cast(&game, angel_id),
        "with two unrestricted instead, the same Angel is castable",
    );
}
