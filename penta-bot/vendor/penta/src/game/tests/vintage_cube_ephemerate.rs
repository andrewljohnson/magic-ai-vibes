//! Ephemerate: one white mana for two enter triggers, a turn apart.

use super::*;

/// Player One holding an Ephemerate, with a creature out that notices
/// arriving.
fn staged() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    game.players[0].exile.clear();
    let blinked = game
        .put_onto_battlefield(PlayerId::One, cards::SNAPCASTER_MAGE)
        .expect("cataloged");
    drain_pending(&mut game);
    settle(&mut game);
    let spell = game
        .build_zone(PlayerId::One, &[cards::EPHEMERATE])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let spell_id = spell.id;
    game.players[0].hand.push(spell);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);
    (game, spell_id, blinked)
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

fn cast(game: &mut Game, spell: GameObjectId, target: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == spell
                    && choices
                        .targets()
                        .iter()
                        .any(|slot| slot.targets().contains(&Target::Permanent(target)))
            }
            _ => false,
        })
        .expect("one white mana casts it at the creature");
    game.apply(PlayerId::One, action).expect("it is cast");
    settle(game);
}

/// The creature that came back, whichever object it is now.
fn blinked_creature(game: &Game) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == ObjectKind::Card(cards::SNAPCASTER_MAGE))
        .expect("it came back")
}

/// It blinks the creature: a new object, still on the battlefield, still
/// under its owner's control.
#[test]
fn it_blinks_the_creature() {
    let (mut game, spell, blinked) = staged();

    cast(&mut game, spell, blinked);

    let returned = blinked_creature(&game);
    assert_ne!(returned.card.id, blinked, "it is a new object");
    assert_eq!(returned.controller, PlayerId::One);
    assert_eq!(
        game.battlefield
            .iter()
            .filter(|permanent| {
                permanent.card.definition == ObjectKind::Card(cards::SNAPCASTER_MAGE)
            })
            .count(),
        1,
        "one of it, not two",
    );
}

/// Cast from hand it is exiled rather than buried, which is the first half
/// of rebound.
#[test]
fn cast_from_hand_it_exiles_itself() {
    let (mut game, spell, blinked) = staged();

    cast(&mut game, spell, blinked);

    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::EPHEMERATE),
        "it is in exile",
    );
    assert!(
        !game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::EPHEMERATE),
        "and not in the graveyard",
    );
}

/// Advances to this player's next upkeep and lets the rebound trigger
/// resolve, stopping at the offer it puts up rather than answering it.
fn to_the_rebound_offer(game: &mut Game) {
    for _ in 0..60 {
        if !game.pending_decisions.is_empty() {
            return;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            game.advance_step();
            continue;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            return;
        }
    }
}

/// The second half: at the next upkeep it is offered back for nothing.
#[test]
fn it_comes_back_at_your_next_upkeep() {
    let (mut game, spell, blinked) = staged();
    cast(&mut game, spell, blinked);
    let exiled = game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::EPHEMERATE)
        .expect("it is in exile")
        .id;

    to_the_rebound_offer(&mut game);

    assert_eq!(game.step, Step::Upkeep, "at an upkeep");
    assert_eq!(game.active_player, PlayerId::One, "and it is yours");
    assert!(
        game.pending_decisions.iter().any(|pending| {
            pending
                .observation
                .options
                .iter()
                .any(|option| option.card.is_some_and(|(object, _)| object == exiled))
        }),
        "the exiled card is offered back",
    );
    assert!(
        game.legal_actions(PlayerId::One)
            .into_iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if card == exiled)),
        "and taking the offer is a free cast",
    );
}

/// The rebounded cast is not from hand, so nothing rebounds a second time:
/// it goes to the graveyard.
#[test]
fn the_rebounded_cast_is_not_exiled_again() {
    let (mut game, spell, blinked) = staged();
    cast(&mut game, spell, blinked);
    let exiled = game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::EPHEMERATE)
        .expect("it is in exile")
        .id;
    to_the_rebound_offer(&mut game);

    let take = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == exiled))
        .expect("the offer is standing");
    game.apply(PlayerId::One, take).expect("it costs nothing");
    settle(&mut game);

    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::EPHEMERATE),
        "buried rather than exiled again",
    );
    assert!(
        !game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::EPHEMERATE),
    );
}

/// And it blinks again on the way through, which is the whole point of
/// paying one mana for it.
#[test]
fn the_rebounded_cast_blinks_again() {
    let (mut game, spell, blinked) = staged();
    cast(&mut game, spell, blinked);
    let after_first = blinked_creature(&game).card.id;
    let exiled = game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::EPHEMERATE)
        .expect("it is in exile")
        .id;
    to_the_rebound_offer(&mut game);

    let take = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == exiled))
        .expect("the offer is standing");
    game.apply(PlayerId::One, take).expect("it costs nothing");
    settle(&mut game);

    let returned = blinked_creature(&game);
    assert_ne!(returned.card.id, after_first, "blinked a second time");
}
