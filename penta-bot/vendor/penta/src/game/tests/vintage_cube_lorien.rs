//! Lórien Revealed: three cards for five mana, or an Island for one.

use super::*;

fn settle(game: &mut Game) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .first()
                .map(|option| vec![option.id])
                .unwrap_or_default();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the decision accepts what it offered");
            continue;
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

/// The card in hand, and a library holding an Island, a Tundra, and a
/// Mountain -- so the search has both a basic and a nonbasic to find.
fn staged() -> (Game, CardInstanceId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for (offset, definition) in [cards::MOUNTAIN, cards::TUNDRA, cards::ISLAND]
        .into_iter()
        .enumerate()
    {
        let id = 82_000 + u32::try_from(offset).expect("a short library");
        game.players[0]
            .library
            .push(card(id, definition, PlayerId::One));
    }
    let spell = card(82_100, cards::LORIEN_REVEALED, PlayerId::One);
    let spell_id = spell.id;
    game.players[0].hand.push(spell);
    (game, spell_id)
}

/// Cast for its printed cost it draws three.
#[test]
fn the_spell_draws_three_cards() {
    let (mut game, spell) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);
    let library = game.players[0].library.len();

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell))
        .expect("five mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);
    drain_pending(&mut game);

    assert_eq!(
        game.players[0].library.len(),
        library - 3,
        "three off the top",
    );
    assert_eq!(game.players[0].hand.len(), 3, "and three in hand");
}

/// Islandcycling costs one, discards the card, and finds an Island.
#[test]
fn islandcycling_finds_an_island_for_one_mana() {
    let (mut game, spell) = staged();
    // Only one Island-typed card, so what comes back is not a coin flip.
    game.players[0]
        .library
        .retain(|card| card.definition != cards::TUNDRA);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let cycle = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == spell))
        .expect("one mana cycles it");
    game.apply(PlayerId::One, cycle).expect("it is activated");
    settle(&mut game);
    drain_pending(&mut game);

    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::LORIEN_REVEALED),
        "the card was discarded as a cost",
    );
    assert!(
        game.players[0]
            .hand
            .iter()
            .any(|card| card.definition == cards::ISLAND),
        "and an Island came back",
    );
    assert!(
        game.players[0]
            .hand
            .iter()
            .all(|card| card.definition != cards::MOUNTAIN),
        "a Mountain has no Island type and was never on offer",
    );
}

/// It names the Island type, not the basic land: a Tundra is an Island too.
#[test]
fn a_dual_land_with_the_island_type_is_findable() {
    let (mut game, spell) = staged();
    game.players[0]
        .library
        .retain(|card| card.definition != cards::ISLAND);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let cycle = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == spell))
        .expect("one mana cycles it");
    game.apply(PlayerId::One, cycle).expect("it is activated");
    settle(&mut game);
    drain_pending(&mut game);

    assert!(
        game.players[0]
            .hand
            .iter()
            .any(|card| card.definition == cards::TUNDRA),
        "a Tundra has the Island type",
    );
}

/// Cycling is a hand ability: with the card anywhere else it is not offered.
#[test]
fn cycling_is_offered_from_hand_only() {
    let (mut game, spell) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    let held = remove_card(&mut game.players[0].hand, spell).expect("it is in hand");
    game.players[0].graveyard.push(held);

    assert!(
        !game.legal_actions(PlayerId::One).iter().any(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == spell)
        ),
        "a discarded card cycles no further",
    );
}
