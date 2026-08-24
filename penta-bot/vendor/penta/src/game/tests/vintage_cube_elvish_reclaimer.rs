//! Elvish Reclaimer: a one-mana body that turns a spent land into whatever
//! the deck is built around, and grows on the graveyard it fills.

use super::*;

/// The Reclaimer out since last turn, with `graveyard` in the graveyard and
/// `library` to search.
fn staged(graveyard: &[CardDefinitionId], library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    game.players[0].library.clear();
    for (index, definition) in graveyard.iter().enumerate() {
        game.players[0].graveyard.push(card(
            240_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    for definition in library {
        let card = game
            .build_zone(PlayerId::One, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].library.push(card);
    }
    let reclaimer = game
        .put_onto_battlefield(PlayerId::One, cards::ELVISH_RECLAIMER)
        .expect("cataloged");
    game.put_onto_battlefield(PlayerId::One, cards::FOREST)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, reclaimer)
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
                .take(decision.maximum.max(decision.minimum))
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

fn stats(game: &Game, id: GameObjectId) -> (Option<i16>, Option<i16>) {
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("he is on the battlefield");
    (game.power(permanent), game.toughness(permanent))
}

/// Two land cards in the graveyard leave him a 1/2; the third makes him a
/// 3/4, and it is read live.
#[test]
fn three_lands_in_the_graveyard_grow_him() {
    let (mut game, reclaimer) = staged(&[cards::MOUNTAIN, cards::ISLAND], &[]);

    assert_eq!(stats(&game, reclaimer), (Some(1), Some(2)));

    game.players[0]
        .graveyard
        .push(card(240_100, cards::SWAMP, PlayerId::One));

    assert_eq!(stats(&game, reclaimer), (Some(3), Some(4)));
}

/// Cards that are not lands do not count.
#[test]
fn other_cards_in_the_graveyard_do_not_count() {
    let (game, reclaimer) = staged(
        &[
            cards::LIGHTNING_BOLT,
            cards::GRIZZLY_BEARS,
            cards::SERRA_ANGEL,
        ],
        &[],
    );

    assert_eq!(stats(&game, reclaimer), (Some(1), Some(2)));
}

/// The activation eats a land and finds one, tapped.
#[test]
fn it_trades_a_land_for_the_one_you_want() {
    let (mut game, reclaimer) = staged(&[], &[cards::MOUNTAIN, cards::ISLAND]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);

    let activate = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::ActivateAbility { source, .. } if *source == reclaimer)
        })
        .expect("two mana, a tap, and a land pay for it");
    game.apply(PlayerId::One, activate).expect("it activates");
    settle(&mut game);

    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::FOREST),
        "the sacrificed land is in the graveyard",
    );
    let found = game
        .battlefield
        .iter()
        .find(|permanent| {
            permanent.card.definition == cards::ISLAND
                || permanent.card.definition == cards::MOUNTAIN
        })
        .expect("a land arrived from the library");
    assert!(found.tapped, "and it arrives tapped");
    assert_eq!(game.players[0].library.len(), 1);
}

/// Without a land to sacrifice there is nothing to activate.
#[test]
fn it_needs_a_land_to_eat() {
    let (mut game, reclaimer) = staged(&[], &[cards::MOUNTAIN]);
    game.battlefield
        .retain(|permanent| permanent.card.definition != cards::FOREST);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);

    assert!(
        !game.legal_actions(PlayerId::One).iter().any(|action| {
            matches!(action, Action::ActivateAbility { source, .. } if *source == reclaimer)
        }),
        "the sacrifice is part of the cost",
    );
}
