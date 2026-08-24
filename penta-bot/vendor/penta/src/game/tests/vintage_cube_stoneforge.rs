//! Stoneforge Mystic: finding an Equipment, then skipping its cost.

use super::*;

/// Answers every pending decision with the last option it offered, then
/// resolves whatever is left on the stack.
fn settle(game: &mut Game) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .last()
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

/// The entry trigger digs an Equipment out of the library and into hand,
/// revealed, and shuffles what is left.
#[test]
fn the_entry_trigger_fetches_an_equipment_into_hand() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for id in 98_000..98_004 {
        game.players[0]
            .library
            .push(card(id, cards::GRIZZLY_BEARS, PlayerId::One));
    }
    game.players[0]
        .library
        .push(card(98_010, cards::SKULLCLAMP, PlayerId::One));

    game.put_onto_battlefield(PlayerId::One, cards::STONEFORGE_MYSTIC)
        .expect("cataloged");
    settle(&mut game);
    drain_pending(&mut game);

    assert!(
        game.players[0]
            .hand
            .iter()
            .any(|card| card.definition == cards::SKULLCLAMP),
        "the Equipment is in hand",
    );
    assert!(
        game.players[0]
            .library
            .iter()
            .all(|card| card.definition != cards::SKULLCLAMP),
        "and out of the library",
    );
    assert!(
        game.players[0]
            .hand
            .iter()
            .all(|card| card.definition != cards::GRIZZLY_BEARS),
        "the search names Equipment, so a creature was never on offer",
    );
}

/// A library with no Equipment offers nothing, and the trigger passes
/// harmlessly.
#[test]
fn a_library_without_an_equipment_finds_nothing() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for id in 98_100..98_104 {
        game.players[0]
            .library
            .push(card(id, cards::GRIZZLY_BEARS, PlayerId::One));
    }

    game.put_onto_battlefield(PlayerId::One, cards::STONEFORGE_MYSTIC)
        .expect("cataloged");
    settle(&mut game);
    drain_pending(&mut game);

    assert!(game.players[0].hand.is_empty(), "nothing was found");
    assert_eq!(game.players[0].library.len(), 4, "and nothing was taken");
}

/// The activated ability puts the Equipment down without casting it, which
/// is the whole reason the card is what it is.
#[test]
fn the_ability_puts_an_equipment_onto_the_battlefield_uncast() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    let mystic = game
        .put_onto_battlefield(PlayerId::One, cards::STONEFORGE_MYSTIC)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.players[0]
        .hand
        .push(card(98_200, cards::UMEZAWAS_JITTE, PlayerId::One));
    game.players[0]
        .hand
        .push(card(98_201, cards::GRIZZLY_BEARS, PlayerId::One));
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let activate = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == mystic),
        )
        .expect("two mana and a tap activates it");
    game.apply(PlayerId::One, activate)
        .expect("the ability activates");
    settle(&mut game);
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::UMEZAWAS_JITTE),
        "the Equipment is on the battlefield",
    );
    assert!(
        game.stack.is_empty(),
        "put down rather than cast, so nothing paid for it",
    );
    assert!(
        game.players[0]
            .hand
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
        "and the creature stayed in hand, never having been on offer",
    );
}
