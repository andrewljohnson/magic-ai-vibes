//! Dark Confidant: an extra card every upkeep, at whatever the top of the
//! library happens to cost.

use super::*;

/// Player One with the Confidant out and `library` stacked so the last
/// entry is on top.
fn staged(library: &[CardDefinitionId]) -> Game {
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
    game.put_onto_battlefield(PlayerId::One, cards::DARK_CONFIDANT)
        .expect("cataloged");
    drain_pending(&mut game);
    game.players[0].life = 20;
    game
}

/// Runs the upkeep trigger of whoever's turn it is now.
fn take_upkeep(game: &mut Game, player: PlayerId) {
    game.active_player = player;
    game.step = Step::Upkeep;
    game.priority = player;
    game.handle_upkeep_triggers();
    for _ in 0..16 {
        if let Some(pending) = game.pending_decisions.first() {
            let decision = pending.observation.clone();
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

/// The card comes off the top and its cost comes off your life total.
#[test]
fn the_upkeep_card_costs_its_mana_value() {
    let mut game = staged(&[cards::GRIZZLY_BEARS]);

    take_upkeep(&mut game, PlayerId::One);

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GRIZZLY_BEARS],
    );
    assert_eq!(game.players[0].life, 18, "a two-drop costs two life");
    assert!(game.players[0].library.is_empty());
}

/// A land is free, which is the whole reason the decks that play him keep
/// their curve down.
#[test]
fn a_land_costs_nothing() {
    let mut game = staged(&[cards::MOUNTAIN]);

    take_upkeep(&mut game, PlayerId::One);

    assert_eq!(game.players[0].life, 20);
    assert_eq!(game.players[0].hand.len(), 1);
}

/// "Your upkeep": the other player's does nothing.
#[test]
fn he_is_silent_on_their_upkeep() {
    let mut game = staged(&[cards::GRIZZLY_BEARS]);

    take_upkeep(&mut game, PlayerId::Two);

    assert_eq!(game.players[0].life, 20);
    assert!(game.players[0].hand.is_empty());
    assert_eq!(game.players[0].library.len(), 1);
}
