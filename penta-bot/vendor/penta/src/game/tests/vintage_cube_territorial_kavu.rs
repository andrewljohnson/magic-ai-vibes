//! Territorial Kavu: as big as the mana base is greedy, with an attack
//! trigger that either loots or eats a graveyard.

use super::*;

/// The Kavu on the battlefield behind `lands`, with `hand` in hand.
fn staged(lands: &[CardDefinitionId], hand: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::One.index()].library.clear();
    game.players[PlayerId::One.index()].graveyard.clear();
    game.players[PlayerId::Two.index()].graveyard.clear();
    game.players[PlayerId::One.index()].library.push(card(
        90_000,
        cards::GIANT_GROWTH,
        PlayerId::One,
    ));
    for (index, definition) in lands.iter().enumerate() {
        game.battlefield.push(creature(
            90_100 + u32::try_from(index).expect("few lands"),
            *definition,
            PlayerId::One,
        ));
    }
    for (index, definition) in hand.iter().enumerate() {
        game.players[PlayerId::One.index()].hand.push(card(
            90_200 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    let kavu = game
        .put_onto_battlefield(PlayerId::One, cards::TERRITORIAL_KAVU)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started[PlayerId::One.index()] = 5;
    (game, kavu)
}

fn size(game: &Game, kavu: GameObjectId) -> (Option<i16>, Option<i16>) {
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == kavu)
        .expect("it is there");
    (game.power(permanent), game.toughness(permanent))
}

/// Declares the Kavu as an attacker and stops on whatever it asks.
fn attack(game: &mut Game, kavu: GameObjectId) {
    game.active_player = PlayerId::One;
    game.step = Step::DeclareAttackers;
    game.priority = PlayerId::One;
    game.declare_attacker(kavu, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
}

/// Answers everything waiting, taking `option` for the first decision.
fn settle(game: &mut Game, first: usize) {
    let mut taken = false;
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let index = if taken { 0 } else { first };
            taken = true;
            let options = decision
                .options
                .iter()
                .skip(index)
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

/// Domain: one basic land type is a 1/1, five are a 5/5.
#[test]
fn it_is_as_big_as_your_basic_land_types() {
    let (game, kavu) = staged(&[cards::MOUNTAIN], &[]);
    assert_eq!(size(&game, kavu), (Some(1), Some(1)));

    let (game, kavu) = staged(
        &[
            cards::MOUNTAIN,
            cards::FOREST,
            cards::ISLAND,
            cards::SWAMP,
            cards::PLAINS,
        ],
        &[],
    );
    assert_eq!(size(&game, kavu), (Some(5), Some(5)));
}

/// Types rather than lands: two Mountains are still one type.
#[test]
fn two_of_a_type_count_once() {
    let (game, kavu) = staged(&[cards::MOUNTAIN, cards::MOUNTAIN, cards::FOREST], &[]);

    assert_eq!(size(&game, kavu), (Some(2), Some(2)));
}

/// The first mode loots: a card goes and a card arrives.
#[test]
fn the_first_mode_loots() {
    let (mut game, kavu) = staged(&[cards::MOUNTAIN], &[cards::MOX_JET]);

    attack(&mut game, kavu);
    settle(&mut game, 0);

    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GIANT_GROWTH],
        "the Mox is discarded and the Growth drawn",
    );
}

/// "If you do": an empty hand discards nothing, and so draws nothing.
#[test]
fn an_empty_hand_draws_nothing() {
    let (mut game, kavu) = staged(&[cards::MOUNTAIN], &[]);

    attack(&mut game, kavu);
    settle(&mut game, 0);

    assert!(game.players[PlayerId::One.index()].hand.is_empty());
    assert_eq!(game.players[PlayerId::One.index()].library.len(), 1);
}

/// The second mode eats a card out of a graveyard.
#[test]
fn the_second_mode_exiles_from_a_graveyard() {
    let (mut game, kavu) = staged(&[cards::MOUNTAIN], &[cards::MOX_JET]);
    game.players[PlayerId::Two.index()].graveyard.push(card(
        90_500,
        cards::SERRA_ANGEL,
        PlayerId::Two,
    ));

    attack(&mut game, kavu);
    settle(&mut game, 1);

    assert!(
        game.players[PlayerId::Two.index()].graveyard.is_empty(),
        "the Angel is exiled",
    );
    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MOX_JET],
        "the other mode was not taken, so nothing was looted",
    );
}

/// The mode is chosen as the trigger goes on the stack, before its targets:
/// a Kavu with an empty board still gets asked.
#[test]
fn the_mode_is_asked_before_anything_else() {
    let (mut game, kavu) = staged(&[cards::MOUNTAIN], &[cards::MOX_JET]);

    attack(&mut game, kavu);
    // Triggers are placed as the step's priority comes around, and the mode
    // is the first thing placement asks.
    let priority = game.priority;
    game.apply(priority, Action::PassPriority).expect("legal");

    let decision = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the mode is asked for");
    assert_eq!(decision.options.len(), 2);
    assert!(
        decision
            .options
            .iter()
            .all(|option| option.ability_text.is_some()),
        "each option names the mode it is",
    );
}
