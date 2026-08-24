//! Quantum Riddler: a Sphinx that draws an extra card whenever your hand is
//! nearly empty, and a warp cost that lends the body for a turn.

use super::*;

/// The Riddler in hand with a stocked library and `hand` beside it.
fn staged(hand: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for index in 0..10 {
        game.players[0]
            .library
            .push(card(102_000 + index, cards::GRIZZLY_BEARS, PlayerId::One));
    }
    for (index, definition) in hand.iter().enumerate() {
        game.players[0].hand.push(card(
            102_100 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    let riddler = game
        .build_zone(PlayerId::One, &[cards::QUANTUM_RIDDLER])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let riddler_id = riddler.id;
    game.players[0].hand.push(riddler);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 5);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, riddler_id)
}

fn resolve(game: &mut Game) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
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

fn cast(game: &mut Game, riddler: GameObjectId, warped: bool) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, choices, .. }
                if *card == riddler && choices.costs().alternative().is_some() == warped)
        })
        .unwrap_or_else(|| panic!("it is castable (warped: {warped})"));
    game.apply(PlayerId::One, action).expect("it is cast");
    resolve(game);
}

/// Entering draws one -- and with an empty hand behind it, the replacement
/// makes that two.
#[test]
fn it_draws_an_extra_card_on_an_empty_hand() {
    let (mut game, riddler) = staged(&[]);

    cast(&mut game, riddler, false);

    assert_eq!(
        game.players[0].hand.len(),
        2,
        "one printed card plus the one the replacement adds",
    );
}

/// With a full hand the replacement does nothing.
#[test]
fn a_full_hand_draws_only_what_the_card_says() {
    let (mut game, riddler) = staged(&[cards::MOX_JET, cards::BLACK_LOTUS]);

    cast(&mut game, riddler, false);

    assert_eq!(
        game.players[0].hand.len(),
        3,
        "the two it started with and the one it drew",
    );
}

/// It replaces the whole instruction rather than each card: a draw of three
/// becomes a draw of four.
#[test]
fn a_larger_draw_gains_exactly_one() {
    let (mut game, riddler) = staged(&[]);
    cast(&mut game, riddler, false);
    game.players[0].hand.clear();

    game.draw_instruction(PlayerId::One, 3);
    resolve(&mut game);

    assert_eq!(game.players[0].hand.len(), 4);
}

/// Warped, the Sphinx arrives for two mana and is exiled at the beginning
/// of the next end step, castable from there afterwards.
#[test]
fn warping_it_lends_the_body_for_a_turn() {
    let (mut game, riddler) = staged(&[]);

    cast(&mut game, riddler, true);
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::QUANTUM_RIDDLER),
        "it arrives for its warp cost",
    );

    game.step = Step::End;
    game.begin_step_triggers();
    resolve(&mut game);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.definition != cards::QUANTUM_RIDDLER),
        "and is exiled at the beginning of the next end step",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::QUANTUM_RIDDLER),
    );

    // A later turn, with the mana to cast it from exile.
    game.turns_started = [6, 6];
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 5);
    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { .. })),
        "it may be cast from exile afterwards",
    );
}

/// Hard cast, nothing exiles it.
#[test]
fn a_hard_cast_one_stays() {
    let (mut game, riddler) = staged(&[]);
    cast(&mut game, riddler, false);

    game.step = Step::End;
    game.begin_step_triggers();
    resolve(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::QUANTUM_RIDDLER),
    );
}
