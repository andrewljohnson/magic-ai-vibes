//! Shelldock Isle: a tapped Island that hides a card until the game is
//! nearly over, and then plays it for nothing.

use super::*;

/// The Isle on the battlefield with `library` stacked on top of player
/// one's library.
fn staged(library: &[CardDefinitionId], library_size: usize) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    game.players[1].library.clear();
    // Filler under the named cards, so the library is as deep as asked.
    let filler = library_size.saturating_sub(library.len());
    for index in 0..filler {
        game.players[0].library.push(card(
            105_000 + u32::try_from(index).expect("few cards"),
            cards::FOREST,
            PlayerId::One,
        ));
    }
    for (index, definition) in library.iter().rev().enumerate() {
        game.players[0].library.push(card(
            105_500 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    for index in 0..40 {
        game.players[1]
            .library
            .push(card(106_000 + index, cards::ISLAND, PlayerId::Two));
    }
    let isle = game
        .put_onto_battlefield(PlayerId::One, cards::SHELLDOCK_ISLE)
        .expect("cataloged");
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, isle)
}

/// Answers the hideaway look by taking `wanted`.
fn hide(game: &mut Game, wanted: CardDefinitionId) {
    for _ in 0..12 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|p| p.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .filter(|option| {
                    option.card.is_some_and(|(_, characteristics)| {
                        characteristics.card_definition() == Some(wanted)
                    })
                })
                .map(|option| option.id)
                .take(1)
                .collect();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the hidden card is one of the four");
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

fn unlock(game: &Game, isle: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == isle))
}

/// It arrives tapped, hides one of the top four, and puts the rest back.
#[test]
fn it_hides_one_of_the_top_four() {
    let (mut game, isle) = staged(
        &[
            cards::BLACK_LOTUS,
            cards::MOX_JET,
            cards::GRIZZLY_BEARS,
            cards::SERRA_ANGEL,
        ],
        30,
    );
    assert!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == isle)
            .expect("it is there")
            .tapped,
        "it enters tapped",
    );

    hide(&mut game, cards::BLACK_LOTUS);

    assert_eq!(
        game.players[0]
            .exile
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::BLACK_LOTUS],
    );
    assert_eq!(
        game.players[0].library.len(),
        29,
        "the other three went back under the library",
    );
    assert!(
        !game
            .legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { .. })),
        "hiding it is not permission to play it",
    );
}

/// With both libraries deep, the unlock is not offered.
#[test]
fn a_full_library_keeps_it_locked() {
    let (mut game, isle) = staged(&[cards::BLACK_LOTUS], 30);
    hide(&mut game, cards::BLACK_LOTUS);
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == isle)
    {
        permanent.tapped = false;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);

    assert!(
        unlock(&game, isle).is_none(),
        "thirty cards is more than twenty",
    );
}

/// Once a library is down to twenty, the hidden card may be played for free.
#[test]
fn twenty_cards_unlocks_it() {
    let (mut game, isle) = staged(&[cards::BLACK_LOTUS], 20);
    hide(&mut game, cards::BLACK_LOTUS);
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == isle)
    {
        permanent.tapped = false;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);

    let unlock = unlock(&game, isle).expect("nineteen cards is fewer than twenty");
    game.apply(PlayerId::One, unlock).expect("it activates");
    for _ in 0..8 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { .. }))
        .expect("the hidden Lotus is castable now");
    game.apply(PlayerId::One, cast).expect("it is cast");
    for _ in 0..8 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::BLACK_LOTUS),
        "and it cost nothing to cast",
    );
}
