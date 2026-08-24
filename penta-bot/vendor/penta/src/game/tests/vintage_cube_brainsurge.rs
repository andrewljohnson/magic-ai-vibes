//! Brainsurge: Brainstorm's two steps for one more card, at instant speed
//! and without a shuffle to hide the two that go back.

use super::*;

/// Player One holding a Brainsurge, with `library` stacked so the last
/// entry is on top.
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
    let card = game
        .build_zone(PlayerId::One, &[cards::BRAINSURGE])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let brainsurge = card.id;
    game.players[0].hand.push(card);
    game.players[0].mana_pool.blue = 1;
    game.players[0].mana_pool.colorless = 2;
    game.turns_started = [1, 1];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, brainsurge)
}

/// Casts and resolves it, stopping at the put-back decision.
fn cast(game: &mut Game, brainsurge: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == brainsurge))
        .expect("three mana casts it");
    game.apply(PlayerId::One, action).expect("it casts");
    for _ in 0..8 {
        if game.observe(PlayerId::One).decision.is_some() {
            return;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// The four cards on top, from the top down.
fn library_top(game: &Game, count: usize) -> Vec<CardDefinitionId> {
    game.players[0]
        .library
        .iter()
        .rev()
        .take(count)
        .map(|card| card.definition)
        .collect()
}

/// Four cards, and then it asks which two go back.
#[test]
fn it_draws_four_and_asks_for_two_back() {
    let (mut game, brainsurge) = staged(&[
        cards::MOUNTAIN,
        cards::PLAINS,
        cards::SWAMP,
        cards::ISLAND,
        cards::FOREST,
    ]);

    cast(&mut game, brainsurge);

    assert_eq!(
        game.players[0].hand.len(),
        4,
        "four cards, and the Brainsurge is on the stack",
    );
    let decision = game
        .observe(PlayerId::One)
        .decision
        .expect("it asks which two go back");
    assert_eq!((decision.minimum, decision.maximum), (2, 2));
}

/// The two named go back on top, the second one named ending up on top.
#[test]
fn the_two_named_go_back_in_the_order_named() {
    let (mut game, brainsurge) = staged(&[
        cards::MOUNTAIN,
        cards::PLAINS,
        cards::SWAMP,
        cards::ISLAND,
        cards::FOREST,
    ]);
    cast(&mut game, brainsurge);
    let decision = game
        .observe(PlayerId::One)
        .decision
        .expect("it asks which two go back");
    let chosen = [cards::FOREST, cards::ISLAND]
        .into_iter()
        .map(|definition| {
            decision
                .options
                .iter()
                .find(|option| {
                    option.card.is_some_and(|(_, characteristics)| {
                        characteristics
                            == ObjectCharacteristics::card(definition, CardPartId::PRIMARY)
                    })
                })
                .expect("the drawn card is on the menu")
                .id
        })
        .collect::<Vec<_>>();

    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: decision.id,
            options: chosen,
        },
    )
    .expect("naming two is legal");
    drain_pending(&mut game);

    assert_eq!(game.players[0].hand.len(), 2, "two of the four stayed");
    assert_eq!(
        library_top(&game, 2),
        vec![cards::ISLAND, cards::FOREST],
        "the card named second is the one drawn first",
    );
}

/// It is an instant, so it can be cast on their turn.
#[test]
fn it_can_be_cast_at_instant_speed() {
    let (mut game, brainsurge) = staged(&[
        cards::MOUNTAIN,
        cards::PLAINS,
        cards::SWAMP,
        cards::ISLAND,
        cards::FOREST,
    ]);
    game.active_player = PlayerId::Two;
    game.step = Step::End;
    game.priority = PlayerId::One;

    assert!(
        game.legal_actions(PlayerId::One)
            .into_iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if card == brainsurge)),
        "an instant is castable in their end step",
    );
}
