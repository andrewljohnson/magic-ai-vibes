//! Barrowgoyf: a body that grows with every graveyard, and a hit that digs
//! for the next creature.

use super::*;

fn settle(game: &mut Game, take: bool) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            // A "may" offers Decline as an option and insists on exactly
            // one answer; the bounded choice inside it accepts none.
            let wanted = if take {
                decision
                    .options
                    .iter()
                    .find(|option| option.label != "Decline")
            } else {
                decision
                    .options
                    .iter()
                    .find(|option| option.label == "Decline")
            };
            let options = wanted.map(|option| vec![option.id]).unwrap_or_default();
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

/// Barrowgoyf out, `graveyard` already in yours, `library` stacked to mill.
fn staged(graveyard: &[CardDefinitionId], library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    game.players[0].library.clear();
    for (offset, definition) in graveyard.iter().enumerate() {
        let id = 80_100 + u32::try_from(offset).expect("a short graveyard");
        game.players[0]
            .graveyard
            .push(card(id, *definition, PlayerId::One));
    }
    // The top of a library is its last element, so the first card milled is
    // the last one pushed.
    for (offset, definition) in library.iter().enumerate().rev() {
        let id = 80_200 + u32::try_from(offset).expect("a short library");
        game.players[0]
            .library
            .push(card(id, *definition, PlayerId::One));
    }
    let goyf = game
        .put_onto_battlefield(PlayerId::One, cards::BARROWGOYF)
        .expect("cataloged");
    drain_pending(&mut game);
    (game, goyf)
}

fn stats(game: &Game, goyf: GameObjectId) -> (Option<i16>, Option<i16>) {
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == goyf)
        .expect("the Lhurgoyf is there");
    (game.power(permanent), game.toughness(permanent))
}

/// Power counts card types across every graveyard; toughness is that plus
/// one.
#[test]
fn it_grows_with_the_card_types_in_all_graveyards() {
    let (game, goyf) = staged(&[], &[]);
    assert_eq!(stats(&game, goyf), (Some(0), Some(1)), "empty graveyards");

    let (mut game, goyf) = staged(&[cards::GRIZZLY_BEARS, cards::MOUNTAIN], &[]);
    assert_eq!(
        stats(&game, goyf),
        (Some(2), Some(3)),
        "a creature and a land is two types",
    );

    // Their graveyard counts too: "all graveyards" is not "yours".
    game.players[1]
        .graveyard
        .push(card(80_300, cards::LIGHTNING_BOLT, PlayerId::Two));
    assert_eq!(
        stats(&game, goyf),
        (Some(3), Some(4)),
        "and an instant of theirs is a third",
    );
}

/// Connecting mills that many and takes a creature from among them.
#[test]
fn combat_damage_mills_and_takes_a_creature() {
    let (mut game, goyf) = staged(
        &[cards::MOUNTAIN],
        &[cards::LIGHTNING_BOLT, cards::SERRA_ANGEL, cards::ISLAND],
    );
    // One card type in the graveyard, so the Lhurgoyf is a 1/2 and hits for
    // one... which mills only one card. Give it more to bite with.
    game.players[0]
        .graveyard
        .push(card(80_310, cards::LIGHTNING_BOLT, PlayerId::One));
    game.players[0]
        .graveyard
        .push(card(80_311, cards::BLACK_LOTUS, PlayerId::One));

    game.damage_target_from_kind(Some(goyf), Some(Target::Player(PlayerId::Two)), 3, true);
    settle(&mut game, true);
    drain_pending(&mut game);

    assert_eq!(
        game.players[0].library.len(),
        0,
        "three cards milled off a three-card library",
    );
    assert!(
        game.players[0]
            .hand
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL),
        "and the creature among them came back",
    );
}

/// Declining the mill leaves the library alone.
#[test]
fn declining_the_mill_takes_nothing() {
    let (mut game, goyf) = staged(
        &[cards::MOUNTAIN, cards::LIGHTNING_BOLT, cards::BLACK_LOTUS],
        &[cards::SERRA_ANGEL, cards::ISLAND, cards::MOUNTAIN],
    );

    game.damage_target_from_kind(Some(goyf), Some(Target::Player(PlayerId::Two)), 3, true);
    settle(&mut game, false);
    drain_pending(&mut game);

    assert_eq!(game.players[0].library.len(), 3, "nothing was milled");
    assert!(game.players[0].hand.is_empty(), "and nothing was taken");
}

/// "From among them" is the milled pile: a creature already in the
/// graveyard is not on offer.
#[test]
fn a_creature_already_in_the_graveyard_is_not_on_offer() {
    let (mut game, goyf) = staged(
        &[cards::GRIZZLY_BEARS, cards::MOUNTAIN, cards::LIGHTNING_BOLT],
        &[cards::ISLAND, cards::MOUNTAIN, cards::LIGHTNING_BOLT],
    );

    game.damage_target_from_kind(Some(goyf), Some(Target::Player(PlayerId::Two)), 3, true);
    settle(&mut game, true);
    drain_pending(&mut game);

    assert_eq!(game.players[0].library.len(), 0, "three were milled");
    assert!(
        game.players[0].hand.is_empty(),
        "no creature was among them, and the Bears in the graveyard stayed",
    );
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
    );
}
