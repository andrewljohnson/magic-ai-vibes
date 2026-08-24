//! Ertai Resurrected: a flash body that answers one thing on the way in and
//! pays for the privilege with the card his victim's controller draws.

use super::*;

/// Player One holding Ertai and a plain creature, with the mana for either,
/// on Player Two's turn.
fn staged() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    game.players[1].library.clear();
    for (index, definition) in [cards::MOUNTAIN, cards::FOREST].into_iter().enumerate() {
        let id = 272_000 + u32::try_from(index).expect("two cards");
        game.players[1]
            .library
            .push(card(id, definition, PlayerId::Two));
    }
    let mut drawn = game
        .build_zone(
            PlayerId::One,
            &[cards::ERTAI_RESURRECTED, cards::SERRA_ANGEL],
        )
        .expect("cataloged")
        .into_iter();
    let ertai = drawn.next().expect("Ertai first");
    let angel = drawn.next().expect("the Angel second");
    let (ertai_id, angel_id) = (ertai.id, angel.id);
    game.players[0].hand.push(ertai);
    game.players[0].hand.push(angel);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::Two;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::Two;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 3);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 3);
    (game, ertai_id, angel_id)
}

fn casts(game: &Game, spell: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell))
        .collect()
}

/// Answers whatever is asked, taking the first offer, until the stack and
/// the triggers are both empty.
fn settle(game: &mut Game) {
    for _ in 0..32 {
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

/// Casts Ertai in the flash window and carries the stack down to the
/// question his trigger asks.
fn flash_ertai(game: &mut Game, ertai: GameObjectId) {
    for _ in 0..4 {
        if game.priority == PlayerId::One {
            break;
        }
        let priority = game.priority;
        game.apply(priority, Action::PassPriority)
            .expect("priority passes");
    }
    let cast = casts(game, ertai)
        .into_iter()
        .next()
        .expect("flash makes him castable");
    game.apply(PlayerId::One, cast).expect("he is cast");
    for _ in 0..16 {
        if !game.pending_decisions.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// Answers the mode question with the mode whose text starts with `prefix`,
/// or with nothing at all, which "up to one" allows.
fn answer_mode(game: &mut Game, prefix: Option<&str>) -> DecisionObservation {
    let decision = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the mode is asked for");
    assert_eq!(decision.player, PlayerId::One, "his controller chooses");
    let options = prefix
        .map(|prefix| {
            decision
                .options
                .iter()
                .find(|option| option.label.starts_with(prefix))
                .map(|option| option.id)
                .expect("that mode is offered")
        })
        .into_iter()
        .collect();
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: decision.id,
            options,
        },
    )
    .expect("the mode choice is legal");
    decision
}

/// Flash: he is castable on their turn, and the plain creature beside him in
/// the same hand is not.
#[test]
fn flash_makes_him_castable_on_their_turn() {
    let (mut game, ertai, angel) = staged();
    let priority = game.priority;
    game.apply(priority, Action::PassPriority)
        .expect("priority passes");

    assert!(!casts(&game, ertai).is_empty(), "flash reaches their turn");
    assert!(
        casts(&game, angel).is_empty(),
        "and an ordinary creature does not",
    );
}

/// The first mode counters a spell, and the player whose spell it was draws.
#[test]
fn the_first_mode_counters_a_spell_and_pays_for_it() {
    let (mut game, ertai, _angel) = staged();
    let theirs = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let theirs_id = theirs.id;
    game.players[1].hand.push(theirs);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::White, 5);
    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == theirs_id))
        .expect("they have the mana");
    game.apply(PlayerId::Two, cast)
        .expect("their spell is cast");

    flash_ertai(&mut game, ertai);
    answer_mode(&mut game, Some("Counter"));
    settle(&mut game);

    assert_eq!(
        game.players[1]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::SERRA_ANGEL],
        "the countered spell is in their graveyard",
    );
    assert_eq!(
        game.battlefield
            .iter()
            .map(|permanent| permanent.card.definition)
            .collect::<Vec<_>>(),
        vec![cards::ERTAI_RESURRECTED],
        "and only Ertai resolved",
    );
    assert_eq!(game.players[1].hand.len(), 1, "its controller drew a card");
    assert_eq!(game.players[1].library.len(), 1);
}

/// "Activated ability" is not decoration: the same mode answers one, and the
/// damage it would have dealt is never dealt.
#[test]
fn the_first_mode_counters_an_activated_ability() {
    let (mut game, ertai, _angel) = staged();
    let sorcerer = game
        .put_onto_battlefield(PlayerId::Two, cards::PRODIGAL_SORCERER)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    let activation = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == sorcerer
                    && targets
                        .iter()
                        .any(|slot| slot.targets().contains(&Target::Player(PlayerId::One)))
            }
            _ => false,
        })
        .expect("the Sorcerer can point at them");
    game.apply(PlayerId::Two, activation)
        .expect("the ability is activated");

    flash_ertai(&mut game, ertai);
    answer_mode(&mut game, Some("Counter"));
    settle(&mut game);

    assert_eq!(game.players[0].life, 20, "the countered ability dealt none");
    assert_eq!(game.players[1].hand.len(), 1, "its controller drew a card");
}

/// The second mode destroys a creature, and its controller draws.
#[test]
fn the_second_mode_destroys_a_creature_and_pays_for_it() {
    let (mut game, ertai, _angel) = staged();
    game.put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);

    flash_ertai(&mut game, ertai);
    answer_mode(&mut game, Some("Destroy"));
    settle(&mut game);

    assert_eq!(
        game.players[1]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GRIZZLY_BEARS],
    );
    assert_eq!(
        game.battlefield
            .iter()
            .map(|permanent| permanent.card.definition)
            .collect::<Vec<_>>(),
        vec![cards::ERTAI_RESURRECTED],
    );
    assert_eq!(game.players[1].hand.len(), 1, "its controller drew a card");
}

/// "Another": the only creature the destroy mode offers is the other one.
#[test]
fn the_second_mode_cannot_name_ertai_himself() {
    let (mut game, ertai, _angel) = staged();
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);

    flash_ertai(&mut game, ertai);
    answer_mode(&mut game, Some("Destroy"));

    let decision = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the mode's own target is asked for");
    assert_eq!(
        decision
            .options
            .iter()
            .filter_map(|option| option.card.map(|(object, _)| object))
            .collect::<Vec<_>>(),
        vec![bears],
        "the other creature, and not Ertai",
    );
}

/// "Up to one": declining every mode is an answer, and the trigger then
/// carries nothing.
#[test]
fn up_to_one_lets_him_answer_nothing() {
    let (mut game, ertai, _angel) = staged();
    let theirs = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let theirs_id = theirs.id;
    game.players[1].hand.push(theirs);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::White, 5);
    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == theirs_id))
        .expect("they have the mana");
    game.apply(PlayerId::Two, cast)
        .expect("their spell is cast");

    flash_ertai(&mut game, ertai);
    let decision = answer_mode(&mut game, None);
    settle(&mut game);

    assert_eq!(decision.minimum, 0, "no mode has to be chosen");
    assert_eq!(decision.options.len(), 2, "both modes are still offered");
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SERRA_ANGEL),
        "their spell resolved",
    );
    assert!(
        game.players[1].hand.is_empty(),
        "and nobody was paid for nothing",
    );
    assert_eq!(game.players[1].library.len(), 2);
}
