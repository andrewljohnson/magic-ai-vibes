//! Consult the Star Charts: a look as deep as your mana base, and twice the
//! keeping when it is kicked.

use super::*;

/// The spell in hand with `lands` lands out and `library` on top.
fn staged(lands: usize, library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    // The back of the library is the top, so the first named card is drawn
    // into the look first.
    for definition in library.iter().rev() {
        let card = game
            .build_zone(PlayerId::One, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].library.push(card);
    }
    for index in 0..lands {
        game.battlefield.push(creature(
            99_000 + u32::try_from(index).expect("few lands"),
            cards::ISLAND,
            PlayerId::One,
        ));
    }
    let consult = game
        .build_zone(PlayerId::One, &[cards::CONSULT_THE_STAR_CHARTS])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let consult_id = consult.id;
    game.players[0].hand.push(consult);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 4);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, consult_id)
}

/// The cast, kicked or not.
fn cast(game: &mut Game, consult: GameObjectId, kicked: bool) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, choices, .. }
                if *card == consult && choices.costs().alternative().is_some() == kicked)
        })
        .unwrap_or_else(|| panic!("it is castable (kicked: {kicked})"));
    game.apply(PlayerId::One, action).expect("it is cast");
    for _ in 0..12 {
        if game.observe(PlayerId::One).decision.is_some() {
            return;
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

fn hand(game: &Game) -> Vec<CardDefinitionId> {
    game.players[0]
        .hand
        .iter()
        .map(|card| card.definition)
        .collect()
}

/// X is the number of lands: three lands look at three cards, and one of
/// them is kept.
#[test]
fn it_looks_as_deep_as_your_lands_and_keeps_one() {
    let (mut game, consult) = staged(
        3,
        &[
            cards::BLACK_LOTUS,
            cards::MOX_JET,
            cards::GRIZZLY_BEARS,
            cards::SERRA_ANGEL,
        ],
    );

    cast(&mut game, consult, false);
    let decision = game
        .observe(PlayerId::One)
        .decision
        .expect("the look asks what to keep");
    assert_eq!(decision.options.len(), 3, "three lands, three cards seen");
    assert_eq!(decision.minimum, 1);
    assert_eq!(decision.maximum, 1, "one of them, unkicked");

    let lotus = decision
        .options
        .iter()
        .find(|option| {
            option.card.is_some_and(|(_, characteristics)| {
                characteristics.card_definition() == Some(cards::BLACK_LOTUS)
            })
        })
        .expect("the Lotus was among them")
        .id;
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: decision.id,
            options: vec![lotus],
        },
    )
    .expect("keeping it is legal");

    assert_eq!(hand(&game), vec![cards::BLACK_LOTUS]);
    assert_eq!(
        game.players[0].library.len(),
        3,
        "the two it passed over went back under the one it never saw",
    );
}

/// Kicked, it keeps two.
#[test]
fn kicked_it_keeps_two() {
    let (mut game, consult) = staged(
        3,
        &[cards::BLACK_LOTUS, cards::MOX_JET, cards::GRIZZLY_BEARS],
    );

    cast(&mut game, consult, true);
    let decision = game
        .observe(PlayerId::One)
        .decision
        .expect("the look asks what to keep");
    assert_eq!(decision.minimum, 2);
    assert_eq!(decision.maximum, 2, "two of them, kicked");

    let chosen = decision
        .options
        .iter()
        .take(2)
        .map(|option| option.id)
        .collect();
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: decision.id,
            options: chosen,
        },
    )
    .expect("keeping two is legal");

    assert_eq!(hand(&game).len(), 2);
}

/// With no lands there is nothing to look at, and the spell simply resolves.
#[test]
fn no_lands_looks_at_nothing() {
    let (mut game, consult) = staged(0, &[cards::BLACK_LOTUS]);

    cast(&mut game, consult, false);

    assert!(game.observe(PlayerId::One).decision.is_none());
    assert!(hand(&game).is_empty(), "nothing was kept");
    assert_eq!(game.players[0].library.len(), 1);
}
